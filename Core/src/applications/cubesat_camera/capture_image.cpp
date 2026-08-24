/*
 * capture_image.cpp
 * Example of capturing a still image using the cubesat_camera modules.
 * Compile with: g++ -std=c++11 -o capture_image capture_image.cpp camera_init/camera_init.cpp camera_config/camera_config.cpp camera_check/camera_check.cpp camera_imaging/camera_imaging.cpp -ljpeg
 */

#include "camera_init.h"
#include "camera_config.h"
#include "camera_check.h"
#include "camera_imaging.h"

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <jpeglib.h>

using namespace cubesat_camera;

/**
 * Convert YUYV (YUV 4:2:2) buffer to JPEG.
 * @param yuyv      Input buffer (width*height*2 bytes)
 * @param width     Image width
 * @param height    Image height
 * @param quality   JPEG quality 1-100
 * @param out_jpeg  Output vector containing JPEG data
 * @return true on success
 */
bool yuyv_to_jpeg(const unsigned char *yuyv, int width, int height,
                  int quality, std::vector<unsigned char> &out_jpeg) {
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
    out_jpeg.assign(jpeg_buffer, jpeg_buffer + jpeg_size);
    free(jpeg_buffer);
    jpeg_destroy_compress(&cinfo);
    return true;
}

int main() {
    CameraContext cam;

    // 1. Open camera
    if (camera_init(cam, "/dev/video0") != 0) {
        fprintf(stderr, "Failed to open camera\n");
        return 1;
    }

    // 2. Check capabilities (optional)
    if (!camera_is_capture_device(cam)) {
        fprintf(stderr, "Not a capture device\n");
        camera_deinit(cam);
        return 1;
    }

    // 3. Configure for still image (YUYV)
    CameraConfig cfg;
    cfg.width       = 1280;   // or 640x480 for faster processing
    cfg.height      = 720;
    cfg.pixelformat = V4L2_PIX_FMT_YUYV;
    cfg.fps         = 1;      // not critical
    cfg.num_buffers = 4;

    if (camera_configure(cam, cfg) != 0) {
        fprintf(stderr, "Configuration failed\n");
        camera_deinit(cam);
        return 1;
    }

    // 4. Capture one still
    Frame frame;
    int ret = camera_capture_still(cam, frame, 5000);
    if (ret != 0) {
        fprintf(stderr, "Capture failed (errno %d)\n", -ret);
        camera_deinit(cam);
        return 1;
    }

    printf("Captured frame: %zu bytes\n", frame.size);

    // 5. Convert YUYV to JPEG
    std::vector<unsigned char> jpeg_data;
    int quality = 80;  // Adjust as needed
    if (yuyv_to_jpeg(static_cast<unsigned char*>(frame.data),
                     cfg.width, cfg.height, quality, jpeg_data)) {
        FILE *fp = fopen("/tmp/capture.jpg", "wb");
        if (fp) {
            fwrite(jpeg_data.data(), 1, jpeg_data.size(), fp);
            fclose(fp);
            printf("JPEG saved: %zu bytes\n", jpeg_data.size());
        } else {
            perror("fopen");
        }
    } else {
        fprintf(stderr, "JPEG conversion failed\n");
    }

    // 6. Release frame and deinit
    camera_release_frame(cam, frame);
    camera_deinit(cam);

    return 0;
}