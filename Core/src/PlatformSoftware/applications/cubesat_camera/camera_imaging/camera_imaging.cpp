#include "camera_imaging.h"

#include <sys/ioctl.h>
#include <poll.h>
#include <cstring>
#include <cerrno>

namespace cubesat_camera {

static int start_streaming(CameraContext &ctx) {
    if (ctx.streaming) return 0;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx.fd, VIDIOC_STREAMON, &type) < 0) {
        return -errno;
    }
    ctx.streaming = true;
    return 0;
}

static void stop_streaming(CameraContext &ctx) {
    if (!ctx.streaming) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(ctx.fd, VIDIOC_STREAMOFF, &type);
    ctx.streaming = false;
}

int camera_capture_still(CameraContext &ctx, Frame &frame, int timeout_ms) {
    if (ctx.fd < 0) return -ENODEV;

    // Ensure streaming is on (we'll turn it off after capture)
    int ret = start_streaming(ctx);
    if (ret < 0) return ret;

    // Wait for a frame to become available
    pollfd pfd;
    pfd.fd     = ctx.fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        stop_streaming(ctx);
        return -errno;
    }
    if (ret == 0) {
        stop_streaming(ctx);
        return -ETIMEDOUT;
    }

    // Dequeue the frame
    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(ctx.fd, VIDIOC_DQBUF, &buf) < 0) {
        stop_streaming(ctx);
        return -errno;
    }

    if (buf.index >= ctx.buffers.size()) {
        stop_streaming(ctx);
        return -EFAULT;
    }

    frame.data      = ctx.buffers[buf.index].start;
    frame.size      = buf.bytesused;
    frame.index     = buf.index;
    frame.timestamp = buf.timestamp;

    // Stop streaming immediately – we only need one frame
    stop_streaming(ctx);

    return 0;
}

int camera_release_frame(CameraContext &ctx, Frame &frame) {
    if (ctx.fd < 0) return -ENODEV;
    if (frame.index >= ctx.buffers.size()) return -EINVAL;

    // Queue the buffer back (ready for next capture)
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