#ifndef VSCRIPT_VSCRIPT_H
#define VSCRIPT_VSCRIPT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define VSCRIPT_API __attribute__((visibility("default")))
#else
    #define VSCRIPT_API
#endif

#define VSCRIPT_VERSION_STRING "1.0.0"

VSCRIPT_API const char* vscript_version(void);

VSCRIPT_API char* vscript_convert_lua(const char* source, const char* options_json);

VSCRIPT_API char* vscript_convert_hx(const char* source, const char* options_json);

VSCRIPT_API char* vscript_convert_shader(const char* source, const char* options_json);

VSCRIPT_API char* vscript_fix_lua(const char* source, const char* options_json);

VSCRIPT_API char* vscript_fix_hx(const char* source, const char* options_json);

VSCRIPT_API char* vscript_generate(const char* spec_json);

VSCRIPT_API char* vscript_builtin_event(const char* name, const char* class_name, const char* id);

VSCRIPT_API char* vscript_convert_data(const char* kind, const char* input_json);

VSCRIPT_API char* vscript_convert_charts(const char* const* difficulties, const char* const* texts, size_t count, const char* options_json, char*** files, size_t* file_count);

VSCRIPT_API char* vscript_convert_files(const char* kind, const char* const* names, const char* const* texts, size_t count, const char* options_json, char*** files, size_t* file_count);

VSCRIPT_API void vscript_free_files(char** files, size_t file_count);

VSCRIPT_API unsigned char* vscript_png_decode(const unsigned char* data, size_t size, int* width, int* height);

VSCRIPT_API unsigned char* vscript_png_encode(const unsigned char* rgba, int width, int height, int level, size_t* size);

VSCRIPT_API unsigned char* vscript_png_optimize(const unsigned char* data, size_t size, int level, size_t* out_size);

VSCRIPT_API unsigned char* vscript_image_resize(const unsigned char* rgba, int width, int height, int new_width, int new_height, int smooth);

VSCRIPT_API void vscript_image_bake(unsigned char* rgba, int width, int height, unsigned int r, unsigned int g, unsigned int b);

VSCRIPT_API void vscript_free_bytes(unsigned char* data);

VSCRIPT_API void vscript_free(char* text);

#ifdef __cplusplus
}
#endif

#endif
