#include "camera_check.h"

#include <sys/ioctl.h>
#include <cstring>
#include <cerrno>

namespace cubesat_camera {

int camera_query_capabilities(CameraContext &ctx, v4l2_capability &cap) {
    if (ctx.fd < 0) return -ENODEV;

    memset(&cap, 0, sizeof(cap));
    if (ioctl(ctx.fd, VIDIOC_QUERYCAP, &cap) < 0) {
        return -errno;
    }
    return 0;
}

bool camera_is_capture_device(CameraContext &ctx) {
    v4l2_capability cap;
    if (camera_query_capabilities(ctx, cap) < 0) {
        return false;
    }
    return (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
}

bool camera_has_streaming(CameraContext &ctx) {
    v4l2_capability cap;
    if (camera_query_capabilities(ctx, cap) < 0) {
        return false;
    }
    return (cap.capabilities & V4L2_CAP_STREAMING) != 0;
}

int camera_check_link(CameraContext &ctx) {
    if (ctx.fd < 0) return -ENODEV;

    // Enumerate the first video input and check its status
    v4l2_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;

    if (ioctl(ctx.fd, VIDIOC_ENUMINPUT, &input) < 0) {
        return -errno;
    }

    if (input.status & V4L2_IN_ST_NO_SIGNAL) {
        return -ENOLINK;
    }
    return 0;
}

} // namespace cubesat_camera