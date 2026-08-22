#include "camera_imaging.h"

#include <sys/ioctl.h>
#include <poll.h>
#include <cstring>
#include <cerrno>

namespace cubesat_camera {

int camera_capture_frame(CameraContext &ctx, Frame &frame, int timeout_ms) {
    if (ctx.fd < 0 || !ctx.streaming) {
        return -ENODEV;
    }

    // Wait for data to become available
    pollfd pfd;
    pfd.fd     = ctx.fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        return -errno;
    }
    if (ret == 0) {
        return -ETIMEDOUT;
    }

    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(ctx.fd, VIDIOC_DQBUF, &buf) < 0) {
        return -errno;
    }

    if (buf.index >= ctx.buffers.size()) {
        return -EFAULT;
    }

    frame.data      = ctx.buffers[buf.index].start;
    frame.size      = buf.bytesused;
    frame.index     = buf.index;
    frame.timestamp = buf.timestamp;

    return 0;
}

int camera_release_frame(CameraContext &ctx, Frame &frame) {
    if (ctx.fd < 0 || !ctx.streaming) {
        return -ENODEV;
    }
    if (frame.index >= ctx.buffers.size()) {
        return -EINVAL;
    }

    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = frame.index;

    if (ioctl(ctx.fd, VIDIOC_QBUF, &buf) < 0) {
        return -errno;
    }

    frame.data  = nullptr;
    frame.size  = 0;
    frame.index = 0;

    return 0;
}

} // namespace cubesat_camera