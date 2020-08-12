#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<unistd.h>

// 编译时加入 -lpthread 选项
#include <thread>

#include <fstream>
#include "X264EncoderTest.h"
#include "BoundedBlockingQueue.hpp"

class H264Data {
public:
    H264Data(int connfd, int deviceId, const char* h264Data, long h264Size) {
        this->connfd = connfd;
        this->deviceId = deviceId;
        this->h264Bytes = h264Data;
        this->h264Size = h264Size;
    }

    int getConnfd() {
        return connfd;
    }

    int getDeviceId() {
        return deviceId;
    }
    
    const char* getH264Bytes() {
        return h264Bytes;
    }

    long getH264Size() {
        return h264Size;
    }
    ~H264Data() {
        delete[] h264Bytes;
    }
private:
    int connfd;
    int deviceId;
    const char* h264Bytes;
    long h264Size;
};

int bytesToInt(unsigned char* bytes, int off);

short bytesToShort(unsigned char* bytes, int off);

void intToBytesBig(unsigned char* bytes, int value);
// template <typename T>
// std::shared_ptr<T> make_shared_array(size_t size)
// {
//     //default_delete是STL中的默认删除器
//     return shared_ptr<T>(new T[size], default_delete<T[]>());
// }

// 连接后首次发送的数据，编码参数设置
// [0, 1] - 宽        short int
// [2, 3] - 高        short int
// [4, 6] - fps 帧数  short int
// [7, 8] - 码率      short int

// 先读取头部 11 个字节
// [0, 3] - 数据长度   int
// [4] - 版本标识      short int
// [5, 8] - 用户标识   int
// [9, 10] - 图片数量  short int
// 再读取下标偏移数组，共（图片数量 * 4）个字节

constexpr auto INIT_PARAM_SIZE = 8;
constexpr auto HEADER_SIZE = 11;

BoundedBlockingQueue<H264Data*> h264Queue(1000);

void receive(int connfd) {
    int count = 0;
    unsigned char initParam[INIT_PARAM_SIZE];
    while(count < INIT_PARAM_SIZE){
        int result = recv(connfd, initParam + count, INIT_PARAM_SIZE - count, 0);
        if(result <= 0){
            close(connfd);
            return;
        }
        count += result;
    }
    int width = bytesToShort(initParam, 0);
    int heigth = bytesToShort(initParam, 2);
    int fps = bytesToShort(initParam, 4);
    int bitrate = bytesToShort(initParam, 6);

    printf("%d %d %d %d\n", width, heigth, fps, bitrate);
    if(!(width && heigth && fps && bitrate)){
        printf("param init failed\n");
        close(connfd);
        return;
    }

    unsigned char headerBuff[HEADER_SIZE];
    unsigned char imgSizeBytes[4];
    while(1) {
        count = 0;
        while(count < HEADER_SIZE) {
            int result = recv(connfd, headerBuff + count, HEADER_SIZE - count, 0);
            if(result <= 0){
                printf("recv header exception\n");
                close(connfd);
                return;
            }
            count += result;
        }
        // 减去头部固定大小 HEADER_SIZE
        int dataLength = bytesToInt(headerBuff, 0) - HEADER_SIZE;
        if(dataLength <= 0) {
            return;
        }

        // 标识占一位， deviceId 偏移为 5

        int deviceId = bytesToInt(headerBuff, 5);
        int imgTotal = bytesToShort(headerBuff, 9);
        if(imgTotal <= 0) {
            printf("close imgTotal:%d\n", imgTotal);
            close(connfd);
            return;
        }

        std::unique_ptr<unsigned char[]> data (new unsigned char[dataLength]);
        count = 0;
        while(count < dataLength) {
            int result = recv(connfd, data.get() + count, dataLength - count, 0);
            if(result <= 0){
                printf("recv data exception\n");
                close(connfd);
                return;
            }
            count += result;
        }

        std::unique_ptr<int[]> imgOffArr(new int[imgTotal]);
        for(int i = 0; i < imgTotal; i++) {
            memcpy(imgSizeBytes, data.get() + (i * 4), 4);
            imgOffArr[i] = bytesToInt(imgSizeBytes, 0);
        }

        int off = imgTotal * 4;
        v1::x264::JpegDataStruct* jpegData = new v1::x264::JpegDataStruct[imgTotal];
        for(int i = 0; i < imgTotal; i++) {
            v1::x264::byte* jpegBytes = new v1::x264::byte[imgOffArr[i]];
            memcpy(jpegBytes, data.get() + off, imgOffArr[i]);

            jpegData[i].jpegSize = imgOffArr[i];
            jpegData[i].jpegBytes = jpegBytes;
            
            off += imgOffArr[i];
        }

        std::unique_ptr<v1::x264::Encoder> encoder(new v1::x264::Encoder());
        if(!encoder.get()->init(width, heigth , fps, bitrate)) {
            printf("encoder init failed\n");
            close(connfd);
            return;
        }
        // H264编码
        long h264Size;
        const char* h264Bytes = encoder.get()->images2h264(jpegData, imgTotal, &h264Size);
        H264Data* h264Data = new H264Data(connfd, deviceId, h264Bytes, h264Size);
        h264Queue.put(h264Data);

        for(int i = 0; i < imgTotal; i++){
            delete[] jpegData[i].jpegBytes;
        }
        delete[] jpegData;
        // 测试使用
        // break;
    }
    close(connfd);
    printf("encode thread end\n");
}

// 发送送的数据格式为
// 头部：
// [0, 3] - 长度数据 int
// [4, 7] - 用户标识 int
// 动态数据： h264数据（动态长度）
void sendData() {
    printf("data send thread start\n");
    unsigned char lenghtBytes[4];
    unsigned char deviceIdBytes[4];

    while(1) {
        H264Data* h264Data = h264Queue.take();
        long size = h264Data->getH264Size() + 8;
        
        intToBytesBig(lenghtBytes, size);
        intToBytesBig(deviceIdBytes, h264Data->getDeviceId());

        //char* data = new char[size];
        //memcpy(data, lenghtBytes, 4);
        //memcpy(data + 4, deviceIdBytes, 4);
        //memcpy(data + 8, h264Data->getH264Bytes(), h264Data->getH264Size());

        if(send(h264Data->getConnfd(), lenghtBytes, 4, 0) < 0){
            close(h264Data->getConnfd());
            continue;
        }
        if(send(h264Data->getConnfd(), deviceIdBytes, 4, 0) < 0){
            close(h264Data->getConnfd());
            continue;
        }
        if(send(h264Data->getConnfd(), h264Data->getH264Bytes(), h264Data->getH264Size(), 0) < 0){
            close(h264Data->getConnfd());
            continue;
        }
        delete[] h264Data;
    }
    printf("data send thread end\n");
}

int main(){
    int  listenfd, connfd;
    struct sockaddr_in  servaddr;

    if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ){
        printf("create socket error: %s(errno: %d)\n",strerror(errno),errno);
        return 0;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(6666);

    if( bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1){
        printf("bind socket error: %s(errno: %d)\n",strerror(errno),errno);
        return 0;
    }

    if( listen(listenfd, 10) == -1){
        printf("listen socket error: %s(errno: %d)\n",strerror(errno),errno);
        return 0;
    }
    
    std::thread sendThread(sendData);
	sendThread.detach();
    printf("======waiting for client's request======\n");
    while(1){
        if( (connfd = accept(listenfd, (struct sockaddr*)NULL, NULL)) == -1){
            printf("accept socket error: %s(errno: %d)",strerror(errno),errno);
            continue;
        }
        std::thread t(receive, connfd);
		t.detach();
    }
    close(listenfd);
    return 0;
}

int bytesToInt(unsigned char* bytes, int off)
{
    int addr = bytes[off + 3] & 0xFF;
    addr |= ((bytes[off + 2] << 8) & 0xFF00);
    addr |= ((bytes[off + 1] << 16) & 0xFF0000);
    addr |= ((bytes[off + 0] << 24) & 0xFF000000);
    return addr;
}

short bytesToShort(unsigned char* bytes, int off)
{
    short addr = bytes[off + 1] & 0xFF;
    addr |= ((bytes[off + 0] << 8) & 0xFF00);
    return addr;
}

void intToBytesBig(unsigned char* bytes , int value) {
    bytes[0] = (unsigned char) ((value >> 24) & 0xFF);
    bytes[1] = (unsigned char) ((value >> 16) & 0xFF);
    bytes[2] = (unsigned char) ((value >> 8) & 0xFF);
    bytes[3] = (unsigned char) (value & 0xFF);
}