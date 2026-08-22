/*
 * CubeSat Camera Still Image Capture
 * For Raspberry Pi Zero W + Pi Camera (V4L2)
 * Captures YUYV frame and converts to JPEG using libjpeg.
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <linux/videodev2.h>
#include <jpeglib.h>

// ---------------------------------------------------------------------
// Camera structures
// ---------------------------------------------------------------------
struct CameraBuffer {
    void*  start  = nullptr;
    size_t length = 0;
};

struct CameraContext {
    int                     fd        = -1;
    std::vector<CameraBuffer> buffers;
    v4l2_format             format{};
    v4l2_requestbuffers     reqbuf{};
    bool                    streaming = false;
};

// ---------------------------------------------------------------------
// V4L2 helper functions
// ---------------------------------------------------------------------

static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int camera_open(CameraContext &ctx, const char *device = "/dev/video0") {
    ctx.fd = open(device, O_RDWR | O_NONBLOCK);
    if (ctx.fd < 0) {
        perror("open camera device");
        return -1;
    }

    v4l2_capability cap;
    if (xioctl(ctx.fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(ctx.fd);
        ctx.fd = -1;
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Device is not a video capture device\n");
        close(ctx.fd);
        ctx.fd = -1;
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming I/O\n");
        close(ctx.fd);
        ctx.fd = -1;
        return -1;
    }

    return 0;
}

int camera_configure(CameraContext &ctx, unsigned int width, unsigned int height,
                     unsigned int pixelformat, unsigned int num_buffers = 4) {
    // Set image format
    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width;
    fmt.fmt.pix.height      = height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (xioctl(ctx.fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }
    ctx.format = fmt;

    // Request buffers
    v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = num_buffers;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx.fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }
    if (req.count < 1) {
        fprintf(stderr, "Insufficient buffer memory\n");
        return -1;
    }
    ctx.reqbuf = req;

    // Map buffers
    ctx.buffers.resize(req.count);
    for (unsigned int i = 0; i < req.count; i++) {
        v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(ctx.fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        ctx.buffers[i].length = buf.length;
        ctx.buffers[i].start = mmap(nullptr, buf.length,
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    ctx.fd, buf.m.offset);
        if (ctx.buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
    }

    // Queue all buffers
    for (unsigned int i = 0; i < ctx.buffers.size(); i++) {
        v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(ctx.fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(ctx.fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    ctx.streaming = true;

    return 0;
}

int camera_capture_frame(CameraContext &ctx, void **data, size_t *size, int timeout_ms = 2000) {
    if (!ctx.streaming) {
        fprintf(stderr, "Streaming not active\n");
        return -1;
    }

    pollfd pfd;
    pfd.fd     = ctx.fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        perror("poll");
        return -1;
    }
    if (ret == 0) {
        fprintf(stderr, "Timeout waiting for frame\n");
        return -1;
    }

    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx.fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return -1;
    }

    if (buf.index >= ctx.buffers.size()) {
        fprintf(stderr, "Invalid buffer index\n");
        return -1;
    }

    *data = ctx.buffers[buf.index].start;
    *size = buf.bytesused;

    // Return the buffer index so we can queue it back later
    return buf.index;
}

int camera_release_buffer(CameraContext &ctx, int index) {
    if (index < 0 || index >= (int)ctx.buffers.size()) {
        return -1;
    }

    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = index;

    if (xioctl(ctx.fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF (release)");
        return -1;
    }
    return 0;
}

void camera_stop(CameraContext &ctx) {
    if (ctx.streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(ctx.fd, VIDIOC_STREAMOFF, &type);
        ctx.streaming = false;
    }

    for (auto &buf : ctx.buffers) {
        if (buf.start != nullptr && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
        }
    }
    ctx.buffers.clear();

    if (ctx.fd >= 0) {
        close(ctx.fd);
        ctx.fd = -1;
    }
}

// ---------------------------------------------------------------------
// YUYV to JPEG conversion using libjpeg
// ---------------------------------------------------------------------
bool yuyv_to_jpeg(const unsigned char *yuyv, int width, int height,
                  int quality, std::vector<unsigned char> &jpeg_out) {
    if (!yuyv || width <= 0 || height <= 0) return false;

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned char *jpeg_buffer = nullptr;
    unsigned long jpeg_size = 0;
    jpeg_mem_dest(&cinfo, &jpeg_buffer, &jpeg_size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    std::vector<unsigned char> rgb_row(width * 3);
    int yuyv_stride = width * 2;

    for (int y = 0; y < height; y++) {
        const unsigned char *src = yuyv + y * yuyv_stride;
        unsigned char *dst = rgb_row.data();

        for (int x = 0; x < width; x += 2) {
            int y0 = src[0];
            int u  = src[1] - 128;
            int y1 = src[2];
            int v  = src[3] - 128;

            // Pixel 1
            int r = y0 + ((v * 1436) >> 10);
            int g = y0 - ((u * 352 + v * 731) >> 10);
            int b = y0 + ((u * 1812) >> 10);
            dst[0] = r < 0 ? 0 : (r > 255 ? 255 : r);
            dst[1] = g < 0 ? 0 : (g > 255 ? 255 : g);
            dst[2] = b < 0 ? 0 : (b > 255 ? 255 : b);

            // Pixel 2
            r = y1 + ((v * 1436) >> 10);
            g = y1 - ((u * 352 + v * 731) >> 10);
            b = y1 + ((u * 1812) >> 10);
            dst[3] = r < 0 ? 0 : (r > 255 ? 255 : r);
            dst[4] = g < 0 ? 0 : (g > 255 ? 255 : g);
            dst[5] = b < 0 ? 0 : (b > 255 ? 255 : b);

            dst += 6;
            src += 4;
        }

        JSAMPROW row_pointer[1];
        row_pointer[0] = rgb_row.data();
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_out.assign(jpeg_buffer, jpeg_buffer + jpeg_size);
    free(jpeg_buffer);
    jpeg_destroy_compress(&cinfo);
    return true;
}

// ---------------------------------------------------------------------
// Main: capture one image and save as JPEG
// ---------------------------------------------------------------------
int main() {
    CameraContext cam;

    // 1. Open camera
    if (camera_open(cam, "/dev/video0") != 0) {
        fprintf(stderr, "Failed to open camera\n");
        return 1;
    }

    // 2. Configure: 1280x720 YUYV
    //    For faster capture/compression, you can use 640x480.
    if (camera_configure(cam, 1280, 720, V4L2_PIX_FMT_YUYV, 4) != 0) {
        fprintf(stderr, "Failed to configure camera\n");
        camera_stop(cam);
        return 1;
    }

    printf("Camera configured: %ux%u\n", cam.format.fmt.pix.width,
           cam.format.fmt.pix.height);

    // 3. Capture one frame
    void *frame_data = nullptr;
    size_t frame_size = 0;
    int buffer_index = camera_capture_frame(cam, &frame_data, &frame_size, 5000);
    if (buffer_index < 0) {
        fprintf(stderr, "Failed to capture frame\n");
        camera_stop(cam);
        return 1;
    }

    printf("Captured frame: %zu bytes\n", frame_size);

    // 4. Convert YUYV to JPEG
    std::vector<unsigned char> jpeg_data;
    int quality = 80;  // Adjust as needed
    if (!yuyv_to_jpeg(static_cast<unsigned char*>(frame_data),
                      cam.format.fmt.pix.width,
                      cam.format.fmt.pix.height,
                      quality, jpeg_data)) {
        fprintf(stderr, "JPEG conversion failed\n");
        camera_release_buffer(cam, buffer_index);
        camera_stop(cam);
        return 1;
    }

    printf("JPEG size: %zu bytes\n", jpeg_data.size());

    // 5. Save to file
    FILE *fp = fopen("/tmp/capture.jpg", "wb");
    if (fp) {
        fwrite(jpeg_data.data(), 1, jpeg_data.size(), fp);
        fclose(fp);
        printf("Image saved to /tmp/capture.jpg\n");
    } else {
        perror("fopen");
    }

    // 6. Release buffer and close camera
    camera_release_buffer(cam, buffer_index);
    camera_stop(cam);

    return 0;
}