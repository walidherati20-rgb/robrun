/* Regression checks for the audit fixes. Production functions, mock memory. */
#include <stdio.h>
#include <string.h>
#include "rr_unity.c"
#include "rr_aim.h"
#include "rr_ascii.h"
static int mock_ok,used=5;
static uint64_t capacity=1;
static size_t readable=8;
static char class_name[160];
int rr_read(uint64_t address, void* output, size_t size) {
    mock_ok=0;
    if((address==0x100000+U_DICT_COUNT||address==0x500000+U_LIST_SIZE)&&size==4) {memcpy(output,&used,4);mock_ok=1;}
    if((address==0x100000+U_DICT_ENTRIES||address==0x500000+U_LIST_ITEMS)&&size==8) {uint64_t array=0x200000;memcpy(output,&array,8);mock_ok=1;}
    if(address==0x200000+U_ARR_LEN&&size==8) {memcpy(output,&capacity,8);mock_ok=1;}
    if(address==0x300000+U_CLASS_NAME&&size==8) {uint64_t string=0x400000;memcpy(output,&string,8);mock_ok=1;}
    if(address==0x400000&&size<=readable) {memcpy(output,class_name,size);mock_ok=1;}
    return mock_ok;
}
uint64_t rr_u64(uint64_t a) {uint64_t v=0;rr_read(a,&v,sizeof(v));return v;}
uint32_t rr_u32(uint64_t a) {uint32_t v=0;rr_read(a,&v,sizeof(v));return v;}
int32_t rr_i32(uint64_t a) {int32_t v=0;rr_read(a,&v,sizeof(v));return v;}
float rr_f32(uint64_t a) {float v=0;rr_read(a,&v,sizeof(v));return v;}
int rr_ok(void) {return mock_ok;}
int game_w2s(const RR_CAMERA* c,const RR_V3* p,int w,int h,float* x,float* y) {
    (void)c;(void)p;(void)w;(void)h;(void)x;(void)y;return 0;
}
int main(void) {
    int failures=0,count=-1,stride=-1;uint64_t entries=1;char output[32];
#define CHECK(c,label) do {int ok=!!(c);printf("[%s] %s\n",ok?"PASS":"FAIL",label);failures+=!ok;}while(0)
    CHECK(!u_valid(0x100003)&&u_valid(0x100000),"object pointer validation enforces alignment");
    CHECK(!u_dict(0x100000,&count,&entries,&stride)&&!count&&!entries&&!stride,"dictionary count above capacity rejected and outputs cleared");
    capacity=5;
    CHECK(u_dict(0x100000,&count,&entries,&stride)==5&&count==5&&entries==0x200020&&stride==24,"valid dictionary remains readable");
    capacity=100001;
    CHECK(!u_dict(0x100000,&count,&entries,&stride),"implausibly large backing array rejected");
    capacity=1;
    CHECK(!u_list(0x500000,&entries)&&!entries,"list count above capacity rejected");
    capacity=5;
    CHECK(u_list(0x500000,&entries)==5&&entries==0x200020,"valid list remains readable");
    memset(class_name,'X',sizeof(class_name));memcpy(class_name,"Head",5);
    CHECK(u_class_name(0x300000,output,sizeof(output))&&!strcmp(output,"Head"),"short successful read with NUL resolves class name");
    memset(class_name,'X',sizeof(class_name));memcpy(class_name,"ABCDEFGH",8);
    CHECK(!u_class_name(0x300000,output,sizeof(output))&&!output[0],"short read without NUL cannot use uninitialized tail");
    memcpy(class_name,"Head",5);
    CHECK(!u_class_name(0x300000,output,4)&&!output[0],"small destination does not accept truncated class identity");
    readable=0;
    CHECK(!u_class_name(0x300000,output,sizeof(output)),"all failed name reads rejected");
    readable=160;memset(class_name,'X',sizeof(class_name));
    CHECK(!u_class_name(0x300000,output,sizeof(output)),"unterminated full name buffer rejected");
    RR_V3 point={0,1,0},previous={0,0,0},current={.008f,0,0},predicted;
    CHECK(rr_aim_motion_point(point,current,previous,8,1,1,&predicted),"8ms aim samples support prediction");
    CHECK(!rr_aim_motion_point(point,current,previous,0,1,1,&predicted),"zero-duration prediction rejected");
    CHECK(!rr_aim_motion_point(point,current,previous,8,51,1,&predicted),"stale prediction sample rejected");
    CHECK(rr_ascii_copy("UnityEntry",11,output,sizeof(output))&&!strcmp(output,"UnityEntry"),
          "UnityEntry name copies without skipping or shifting characters");
    CHECK(!rr_ascii_copy("UnityEntry",8,output,sizeof(output))&&!output[0],
          "short scene-name read without terminator is rejected");
    CHECK(!rr_ascii_copy("UnityEntry",11,output,10),"scene-name destination must fit the terminator");
    CHECK(!rr_ascii_copy("Bad\nName",9,output,sizeof(output)),"scene-name control characters rejected");
    CHECK(!rr_ascii_copy("",1,output,sizeof(output)),"empty scene name rejected");
    CHECK(!rr_ascii_copy(NULL,0,output,sizeof(output)),"null scene-name source rejected");
    CHECK(!rr_ascii_copy("UnityEntry",11,NULL,0),"null scene-name destination rejected");
    printf("Mock memory only. No process, backend or input accessed.\n");
    return failures?1:0;
}
