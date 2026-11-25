#include <jni.h>

#include <string>

#include "vbanana/VBanana.h"

#define VCB_FUNCTION(name) Java_com_vconvertercrew_vconverter_tools_online_VBananaNative_##name

namespace {

std::string bytes(JNIEnv* env, jbyteArray value) {
    if (value == nullptr) return std::string();
    jsize length = env->GetArrayLength(value);
    std::string out(static_cast<size_t>(length), '\0');
    if (length > 0) env->GetByteArrayRegion(value, 0, length, reinterpret_cast<jbyte*>(&out[0]));
    return out;
}

jbyteArray take(JNIEnv* env, char* result) {
    if (result == nullptr) return nullptr;
    std::string text(result);
    vbanana_free(result);
    jbyteArray out = env->NewByteArray(static_cast<jsize>(text.size()));
    if (out != nullptr && !text.empty()) env->SetByteArrayRegion(out, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
    return out;
}

}

extern "C" {

JNIEXPORT jstring JNICALL VCB_FUNCTION(nativeVersion)(JNIEnv* env, jclass) {
    return env->NewStringUTF(vbanana_version());
}

JNIEXPORT jbyteArray JNICALL VCB_FUNCTION(nativeRequest)(JNIEnv* env, jclass, jbyteArray request) {
    std::string json = bytes(env, request);
    return take(env, vbanana_request(json.c_str()));
}

JNIEXPORT jbyteArray JNICALL VCB_FUNCTION(nativeParse)(JNIEnv* env, jclass, jbyteArray kind, jbyteArray body, jbyteArray options) {
    std::string k = bytes(env, kind);
    std::string b = bytes(env, body);
    std::string o = bytes(env, options);
    return take(env, vbanana_parse(k.c_str(), b.c_str(), o.c_str()));
}

JNIEXPORT jbyteArray JNICALL VCB_FUNCTION(nativeLink)(JNIEnv* env, jclass, jbyteArray text) {
    std::string t = bytes(env, text);
    return take(env, vbanana_link(t.c_str()));
}

JNIEXPORT jbyteArray JNICALL VCB_FUNCTION(nativeInspect)(JNIEnv* env, jclass, jbyteArray entries) {
    std::string e = bytes(env, entries);
    return take(env, vbanana_inspect(e.c_str()));
}

JNIEXPORT jstring JNICALL VCB_FUNCTION(nativeMd5File)(JNIEnv* env, jclass, jbyteArray path) {
    std::string p = bytes(env, path);
    char* result = vbanana_md5_file(p.c_str());
    if (result == nullptr) return nullptr;
    jstring out = env->NewStringUTF(result);
    vbanana_free(result);
    return out;
}

}
