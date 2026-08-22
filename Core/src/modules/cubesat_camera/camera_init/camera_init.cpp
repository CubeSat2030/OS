#include "camera_init.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace cubesat_camera {

int camera_init(CameraContext &ctx, const std::string &device) {
    if (ctx.fd >= 0) {
        return -EBUSY; // already open
    }

    ctx.device = device;

    // Open device (non-blocking so we can use poll() later)
    ctx.fd = open(device.c_str(), O_RDWR | O_NONBLOCK);
    if (ctx.fd < 0) {
        return -errno;
    }

    // Query capabilities
    v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(ctx.fd, VIDIOC_QUERYCAP, &cap) < 0) {
        int err = errno;
        close(ctx.fd);
        ctx.fd = -1;
        return -err;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        close(ctx.fd);
        ctx.fd = -1;
        return -ENODEV;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        close(ctx.fd);
        ctx.fd = -1;
        return -ENOTSUP;
    }

    return 0;
}

void camera_deinit(CameraContext &ctx) {
    if (ctx.fd < 0) {
        return;
    }

    // Stop streaming if active
    if (ctx.streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(ctx.fd, VIDIOC_STREAMOFF, &type);
        ctx.streaming = false;
    }

    // Unmap all buffers
    for (auto &buf : ctx.buffers) {
        if (buf.start != nullptr && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
        }
    }
    ctx.buffers.clear();

    close(ctx.fd);
    ctx.fd = -1;
}

} // namespace cubesat_camera