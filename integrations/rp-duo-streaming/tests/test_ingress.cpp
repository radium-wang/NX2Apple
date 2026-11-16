#include "raw_haptics_ingress.h"
#include <cassert>
#include <cstdio>
int main() {
    unsigned char frame[64] {}; memcpy(frame,"RPH1",4); frame[4]=1; frame[28]=0x40; frame[29]=0x0d; frame[30]=3;
    assert(rp_haptics_valid(frame,64)); assert(!rp_haptics_valid(frame,63));
    frame[7]=1; assert(!rp_haptics_valid(frame,64)); frame[7]=0;
    float hz=164.81f; memcpy(frame+36,&hz,4);
    float bad=NAN; memcpy(frame+32,&bad,4); assert(!rp_haptics_valid(frame,64)); memset(frame+32,0,4);
    unsigned char timed[64]; memcpy(timed,frame,64);
    uint64_t source=1230000; uint32_t ttl=90000; float amplitude=.1f;
    memcpy(timed+20,&source,8); memcpy(timed+28,&ttl,4); memcpy(timed+32,&amplitude,4);
    assert(!rp_haptics_apply_host_age(timed,source-1));
    assert(!rp_haptics_apply_host_age(timed,source+55001));
    assert(rp_haptics_apply_host_age(timed,source+12000));
    assert(rp_haptics_u32(timed+20)==source+12000 && rp_haptics_u32(timed+28)==78000);
    assert(!memcmp(timed+36,&hz,4));
    memset(timed+32,0,4);
    assert(rp_haptics_apply_host_age(timed,source+150000));
    assert(rp_haptics_u32(timed+20)==source+150000);
    std::string path="/private/tmp/rp-haptics-test-"+std::to_string(getpid())+".sock";
    {
        rpduo::haptics_ingress ingress(path); assert(ingress.ready());
        rpduo::haptics_ingress second(path); assert(!second.ready());
        assert(ingress.poll().empty());
        int fd=socket(AF_UNIX,SOCK_DGRAM,0); sockaddr_un addr {}; addr.sun_family=AF_UNIX; strcpy(addr.sun_path,path.c_str());
        assert(sendto(fd,frame,64,0,(sockaddr*)&addr,sizeof(addr))==64);
        assert(sendto(fd,frame,63,0,(sockaddr*)&addr,sizeof(addr))==63);
        frame[8]=2; assert(sendto(fd,frame,64,0,(sockaddr*)&addr,sizeof(addr))==64);
        auto frames=ingress.poll(); assert(frames.size()==2 && frames[0][8]==0 && frames[1][8]==2);
        assert(!memcmp(frames[0].data()+36,&hz,4)); close(fd);
        struct stat st {}; assert(!stat(path.c_str(),&st) && (st.st_mode&0777)==0600);
    }
    assert(access(path.c_str(),F_OK)); unlink((path+".lock").c_str());
    puts("raw validation, host aging, exact frequency preservation, FIFO transitions and private socket: PASS");
}
