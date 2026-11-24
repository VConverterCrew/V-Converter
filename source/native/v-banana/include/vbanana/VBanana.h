#ifndef VBANANA_H
#define VBANANA_H

#if defined(_WIN32) && defined(VBANANA_SHARED)
#define VBANANA_API __declspec(dllexport)
#else
#define VBANANA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

VBANANA_API const char* vbanana_version(void);

VBANANA_API char* vbanana_request(const char* request_json);

VBANANA_API char* vbanana_parse(const char* kind, const char* body, const char* options_json);

VBANANA_API char* vbanana_link(const char* text);

VBANANA_API char* vbanana_inspect(const char* entries_json);

VBANANA_API char* vbanana_md5(const unsigned char* data, unsigned long long length);

VBANANA_API char* vbanana_md5_file(const char* path);

VBANANA_API void vbanana_free(char* text);

#ifdef __cplusplus
}
#endif

#endif
