/* Actual transport implementation with every device/registry/IO entry mocked.
 * No device, process or registry is opened by this executable. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
static FARPROC WINAPI mock_getproc(HMODULE module,LPCSTR name);
static HANDLE WINAPI mock_create(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static BOOL WINAPI mock_close(HANDLE);
static LSTATUS WINAPI mock_regopen(HKEY,LPCSTR,DWORD,REGSAM,PHKEY);
static LSTATUS WINAPI mock_regquery(HKEY,LPCSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
static LSTATUS WINAPI mock_regclose(HKEY);
#define GetProcAddress mock_getproc
#define CreateFileA mock_create
#define CloseHandle mock_close
#define RegOpenKeyExA mock_regopen
#define RegQueryValueExA mock_regquery
#define RegCloseKey mock_regclose
#include "rr_mem.c"
static int permit_open,create_calls,close_calls,fail_io;
static DWORD token_type=REG_DWORD;
static NTSTATUS WINAPI mock_ntcreate(PHANDLE h,ACCESS_MASK access,POBJECT_ATTRIBUTES attributes,
    PIO_STATUS_BLOCK ios,PLARGE_INTEGER allocation,ULONG file_attributes,ULONG share,
    ULONG disposition,ULONG options,PVOID ea,ULONG ea_length) {
    (void)access;(void)attributes;(void)ios;(void)allocation;(void)file_attributes;
    (void)share;(void)disposition;(void)options;(void)ea;(void)ea_length;
    *h=NULL;return (NTSTATUS)0xC0000034;
}
static NTSTATUS WINAPI mock_ioctl(HANDLE h,HANDLE event,PIO_APC_ROUTINE callback,PVOID context,
    PIO_STATUS_BLOCK ios,ULONG code,PVOID input,ULONG input_size,PVOID output,ULONG output_size) {
    (void)h;(void)event;(void)callback;(void)context;(void)code;(void)input;(void)input_size;
    if(fail_io) return (NTSTATUS)0xC0000001;
    memset(output,0,output_size);ios->Information=output_size;return 0;
}
static FARPROC WINAPI mock_getproc(HMODULE module,LPCSTR name) {
    (void)module;
    if(!strcmp(name,"NtCreateFile"))return (FARPROC)mock_ntcreate;
    if(!strcmp(name,"NtDeviceIoControlFile"))return (FARPROC)mock_ioctl;
    return NULL;
}
static HANDLE WINAPI mock_create(LPCSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES sa,DWORD d,DWORD f,HANDLE t) {
    (void)p;(void)a;(void)s;(void)sa;(void)d;(void)f;(void)t;++create_calls;
    return permit_open?(HANDLE)(ULONG_PTR)0x1234:INVALID_HANDLE_VALUE;
}
static BOOL WINAPI mock_close(HANDLE h) { (void)h;++close_calls;return TRUE; }
static LSTATUS WINAPI mock_regopen(HKEY k,LPCSTR p,DWORD o,REGSAM a,PHKEY result) {
    (void)k;(void)p;(void)o;(void)a;*result=(HKEY)(ULONG_PTR)1;return ERROR_SUCCESS;
}
static LSTATUS WINAPI mock_regquery(HKEY k,LPCSTR p,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size) {
    DWORD token=7;(void)k;(void)p;(void)reserved;*type=token_type;*size=sizeof(token);memcpy(data,&token,sizeof(token));return ERROR_SUCCESS;
}
static LSTATUS WINAPI mock_regclose(HKEY key) { (void)key;return ERROR_SUCCESS; }
int main(void) {
    int failures=0;unsigned value=99;
#define CHECK(c,label) do { int ok=!!(c);printf("[%s] %s\n",ok?"PASS":"FAIL",label);failures+=!ok; }while(0)
    CHECK(!rr_drv_open()&&!g_dev&&!rr_drv_ready(),"failed open leaves no invalid handle");
    CHECK(!rr_drv_open()&&create_calls==2&&!g_dev,"repeated failure retries the actual open");
    permit_open=1;
    CHECK(rr_drv_open()&&rr_drv_ready(),"open recovers after earlier failures");
    CHECK(rr_read(0x100000,&value,sizeof(value))&&rr_ok()&&value==0,"successful zero read remains distinguishable");
    fail_io=1;
    CHECK(!rr_read(0x100000,&value,sizeof(value))&&!rr_ok(),"failed direct read clears prior success status");
    rr_drv_close();
    CHECK(!g_dev&&!g_drvkey&&!rr_drv_ready(),"close resets handle and key");
    token_type=REG_SZ;
    CHECK(!rr_drv_open()&&!g_dev&&!g_drvkey,"wrong registry token type is rejected");
    int closed=close_calls;g_dev=INVALID_HANDLE_VALUE;g_drvkey=42;
    CHECK(!rr_drv_ready(),"invalid handle never reports ready");
    rr_drv_close();
    CHECK(close_calls==closed&&!g_dev&&!g_drvkey,"close skips invalid handle and clears state");
    printf("No backend or registry accessed; all external operations mocked.\n");
    return failures?1:0;
}
