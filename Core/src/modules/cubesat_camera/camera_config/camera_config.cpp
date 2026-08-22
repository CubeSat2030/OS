#include "camera_config.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <cerrno>

namespace cubesat_camera {

int camera_set_fps(CameraContext &ctx, unsigned int fps) {
    if (ctx.fd < 0) return -ENODEV;

    v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = fps;

    if (ioctl(ctx.fd, VIDIOC_S_PARM, &parm) < 0) {
        return -errno;
    }
    return 0;
}

int camera_set_exposure(CameraContext &ctx, int exposure_us) {
    if (ctx.fd < 0) return -ENODEV;

    v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id    = V4L2_CID_EXPOSURE;
    ctrl.value = exposure_us;

    if (ioctl(ctx.fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        return -errno;
    }
    return 0;
}

int camera_configure(CameraContext &ctx, const CameraConfig &cfg) {
    if (ctx.fd < 0) return -ENODEV;

    // If already streaming, stop and clean up old buffers
    if (ctx.streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(ctx.fd, VIDIOC_STREAMOFF, &type);
        ctx.streaming = false;
    }
    for (auto &buf : ctx.buffers) {
        if (buf.start != nullptr && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
        }
    }
    ctx.buffers.clear();

    // 1. Set image format
    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = cfg.width;
    fmt.fmt.pix.height      = cfg.height;
    fmt.fmt.pix.pixelformat = cfg.pixelformat;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (ioctl(ctx.fd, VIDIOC_S_FMT, &fmt) < 0) {
        return -errno;
    }
    ctx.format = fmt;

    // 2. Set frame rate (ignore if unsupported)
    int ret = camera_set_fps(ctx, cfg.fps);
    if (ret < 0 && ret != -ENOTTY) {
        return ret;
    }

    // 3. Request memory‑mapped buffers
    v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = cfg.num_buffers;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(ctx.fd, VIDIOC_REQBUFS, &req) < 0) {
        return -errno;
    }
    if (req.count < 1) {
        return -ENOMEM;
    }
    ctx.reqbuf = req;

    // 4. Map buffers into user space
    ctx.buffers.resize(req.count);
    for (unsigned int i = 0; i < req.count; i++) {
        v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (ioctl(ctx.fd, VIDIOC_QUERYBUF, &buf) < 0) {
            for (unsigned int j = 0; j < i; j++) {
                munmap(ctx.buffers[j].start, ctx.buffers[j].length);
            }
            ctx.buffers.clear();
            return -errno;
        }

        void *ptr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                         MAP_SHARED, ctx.fd, buf.m.offset);
        if (ptr == MAP_FAILED) {
            int err = errno;
            for (unsigned int j = 0; j < i; j++) {
                munmap(ctx.buffers[j].start, ctx.buffers[j].length);
            }
            ctx.buffers.clear();
            return -err;
        }

        ctx.buffers[i].start  = ptr;
        ctx.buffers[i].length = buf.length;
    }

    // 5. Queue all buffers
    for (unsigned int i = 0; i < ctx.buffers.size(); i++) {
        v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (ioctl(ctx.fd, VIDIOC_QBUF, &buf) < 0) {
            for (auto &b : ctx.buffers) {
                munmap(b.start, b.length);
            }
            ctx.buffers.clear();
            return -errno;
        }
    }

    // 6. Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx.fd, VIDIOC_STREAMON, &type) < 0) {
        for (auto &b : ctx.buffers) {
            munmap(b.start, b.length);
        }
        ctx.buffers.clear();
        return -errno;
    }

    ctx.streaming = true;
    return 0;
}

} // namespace cubesat_camera