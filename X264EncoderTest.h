#pragma once
extern "C" {
#include "x264.h"
}
#include <memory>
namespace v1 {
    namespace x264 {
        typedef unsigned char byte;

        typedef struct JpegDataStruct {
            unsigned long jpegSize;
            byte* jpegBytes;
        } JpegDataStruct;

        typedef struct YuvDataStruct {
            int frameSize;
            byte* yuvBytes;
        } YuvDataStruct;
        
        class Encoder {
        public:
            const char* images2h264(JpegDataStruct* jpgBuffers, int frameTotal, long* x264Size);
            int jpeg2yuv(unsigned char* yuv_buffer, unsigned char* jpg_buffer, unsigned long jpg_size, int height, int width);
            bool init(int width, int height, int fps, int bitrate);
            void release();
            ~Encoder() {
                release();
            }
        private:
            // 编码参数
            x264_param_t param;
            // 存储编码后的图像
            x264_picture_t pic;
            x264_picture_t pic_out;
            // 编码器
            x264_t* h = NULL;
            // 编码后的数据
            x264_nal_t* nal;
            int i_nal;

            int width;
            int height;

            unsigned int luma_size;
            int chroma_size;
            unsigned long yuv_size;
            byte* yuv_buffer;
            byte* luma_data;
            byte* chroma_data_1;
            byte* chroma_data_2;
        };
    }
}