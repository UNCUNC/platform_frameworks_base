/**
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "radio.convert.jni"
#define LOG_NDEBUG 0

#include "com_android_server_radio_convert.h"

#include <JNIHelp.h>
#include <Utils.h>
#include <core_jni_helpers.h>
#include <utils/Log.h>

namespace android {
namespace server {
namespace radio {
namespace convert {

using hardware::Return;
using hardware::hidl_vec;

using V1_0::Band;
using V1_0::Deemphasis;
using V1_0::Direction;
using V1_0::MetadataType;
using V1_0::Result;
using V1_0::Rds;
using V1_1::ProgramIdentifier;
using V1_1::ProgramListResult;
using V1_1::ProgramSelector;

static JavaRef<jobject> BandDescriptorFromHal(JNIEnv *env, const V1_0::BandConfig &config, Region region);

static struct {
    struct {
        jfieldID descriptor;
    } BandConfig;
    struct {
        jclass clazz;
        jmethodID cstor;
        jfieldID stereo;
        jfieldID rds;
        jfieldID ta;
        jfieldID af;
        jfieldID ea;
    } FmBandConfig;
    struct {
        jclass clazz;
        jmethodID cstor;
        jfieldID stereo;
    } AmBandConfig;

    struct {
        jclass clazz;
        jfieldID region;
        jfieldID type;
        jfieldID lowerLimit;
        jfieldID upperLimit;
        jfieldID spacing;
    } BandDescriptor;
    struct {
        jclass clazz;
        jmethodID cstor;
    } FmBandDescriptor;
    struct {
        jclass clazz;
        jmethodID cstor;
    } AmBandDescriptor;

    struct {
        jclass clazz;
        jmethodID cstor;
    } ModuleProperties;

    struct {
        jclass clazz;
        jmethodID cstor;
    } ProgramInfo;

    struct {
        jclass clazz;
        jmethodID cstor;
        jfieldID programType;
        jfieldID primaryId;
        jfieldID secondaryIds;
        jfieldID vendorIds;

        struct {
            jclass clazz;
            jmethodID cstor;
            jfieldID type;
            jfieldID value;
        } Identifier;
    } ProgramSelector;

    struct {
        jclass clazz;
        jmethodID cstor;
        jmethodID putIntFromNative;
        jmethodID putStringFromNative;
        jmethodID putBitmapFromNative;
        jmethodID putClockFromNative;
    } RadioMetadata;

    struct {
        jclass clazz;
        jmethodID cstor;
    } RuntimeException;

    struct {
        jclass clazz;
        jmethodID cstor;
    } ParcelableException;
} gjni;

bool __ThrowIfFailedHidl(JNIEnv *env, const hardware::details::return_status &hidlResult) {
    if (hidlResult.isOk()) return false;

    ThrowParcelableRuntimeException(env, "HIDL call failed: " + hidlResult.description());
    return true;
}

bool __ThrowIfFailed(JNIEnv *env, const Result halResult) {
    switch (halResult) {
        case Result::OK:
            return false;
        case Result::NOT_INITIALIZED:
            ThrowParcelableRuntimeException(env, "Result::NOT_INITIALIZED");
            return true;
        case Result::INVALID_ARGUMENTS:
            jniThrowException(env, "java/lang/IllegalArgumentException",
                    "Result::INVALID_ARGUMENTS");
            return true;
        case Result::INVALID_STATE:
            jniThrowException(env, "java/lang/IllegalStateException", "Result::INVALID_STATE");
            return true;
        case Result::TIMEOUT:
            ThrowParcelableRuntimeException(env, "Result::TIMEOUT (unexpected here)");
            return true;
        default:
            ThrowParcelableRuntimeException(env, "Unknown failure, result: "
                    + std::to_string(static_cast<int32_t>(halResult)));
            return true;
    }
}

bool __ThrowIfFailed(JNIEnv *env, const ProgramListResult halResult) {
    switch (halResult) {
        case ProgramListResult::NOT_READY:
            jniThrowException(env, "java/lang/IllegalStateException", "Scan is in progress");
            return true;
        case ProgramListResult::NOT_STARTED:
            jniThrowException(env, "java/lang/IllegalStateException", "Scan has not been started");
            return true;
        case ProgramListResult::UNAVAILABLE:
            ThrowParcelableRuntimeException(env,
                    "ProgramListResult::UNAVAILABLE (unexpected here)");
            return true;
        default:
            return __ThrowIfFailed(env, static_cast<Result>(halResult));
    }
}

void ThrowParcelableRuntimeException(JNIEnv *env, const std::string& msg) {
    auto jMsg = make_javastr(env, msg);
    auto runtimeExc = make_javaref(env, env->NewObject(gjni.RuntimeException.clazz,
            gjni.RuntimeException.cstor, jMsg.get()));
    auto parcelableExc = make_javaref(env, env->NewObject(gjni.ParcelableException.clazz,
            gjni.ParcelableException.cstor, runtimeExc.get()));

    auto res = env->Throw(static_cast<jthrowable>(parcelableExc.get()));
    ALOGE_IF(res != JNI_OK, "Couldn't throw parcelable runtime exception");
}

static JavaRef<jintArray> ArrayFromHal(JNIEnv *env, const hidl_vec<uint32_t>& vec) {
    auto jArr = make_javaref(env, env->NewIntArray(vec.size()));
    auto jArrElements = env->GetIntArrayElements(jArr.get(), nullptr);
    for (size_t i = 0; i < vec.size(); i++) {
        jArrElements[i] = vec[i];
    }
    env->ReleaseIntArrayElements(jArr.get(), jArrElements, 0);
    return jArr;
}

static JavaRef<jlongArray> ArrayFromHal(JNIEnv *env, const hidl_vec<uint64_t>& vec) {
    auto jArr = make_javaref(env, env->NewLongArray(vec.size()));
    auto jArrElements = env->GetLongArrayElements(jArr.get(), nullptr);
    for (size_t i = 0; i < vec.size(); i++) {
        jArrElements[i] = vec[i];
    }
    env->ReleaseLongArrayElements(jArr.get(), jArrElements, 0);
    return jArr;
}

template <typename T>
static JavaRef<jobjectArray> ArrayFromHal(JNIEnv *env, const hidl_vec<T>& vec,
        jclass jElementClass, std::function<JavaRef<jobject>(JNIEnv*, const T&)> converter) {
    auto jArr = make_javaref(env, env->NewObjectArray(vec.size(), jElementClass, nullptr));
    for (size_t i = 0; i < vec.size(); i++) {
        auto jElement = converter(env, vec[i]);
        env->SetObjectArrayElement(jArr.get(), i, jElement.get());
    }
    return jArr;
}

template <typename T>
static JavaRef<jobjectArray> ArrayFromHal(JNIEnv *env, const hidl_vec<T>& vec,
        jclass jElementClass, JavaRef<jobject>(*converter)(JNIEnv*, const T&)) {
    return ArrayFromHal(env, vec, jElementClass,
            std::function<JavaRef<jobject>(JNIEnv*, const T&)>(converter));
}

static Rds RdsForRegion(bool rds, Region region) {
    if (!rds) return Rds::NONE;

    switch(region) {
        case Region::ITU_1:
        case Region::OIRT:
        case Region::JAPAN:
        case Region::KOREA:
            return Rds::WORLD;
        case Region::ITU_2:
            return Rds::US;
        default:
            ALOGE("Unexpected region: %d", region);
            return Rds::NONE;
    }
}

static Deemphasis DeemphasisForRegion(Region region) {
    switch(region) {
        case Region::KOREA:
        case Region::ITU_2:
            return Deemphasis::D75;
        case Region::ITU_1:
        case Region::OIRT:
        case Region::JAPAN:
            return Deemphasis::D50;
        default:
            ALOGE("Unexpected region: %d", region);
            return Deemphasis::D50;
    }
}

static JavaRef<jobject> ModulePropertiesFromHal(JNIEnv *env, const V1_0::Properties &prop10,
        const V1_1::Properties *prop11, jint moduleId, const std::string& serviceName) {
    ALOGV("ModulePropertiesFromHal()");
    using namespace std::placeholders;

    auto jServiceName = make_javastr(env, serviceName);
    auto jImplementor = make_javastr(env, prop10.implementor);
    auto jProduct = make_javastr(env, prop10.product);
    auto jVersion = make_javastr(env, prop10.version);
    auto jSerial = make_javastr(env, prop10.serial);
    bool isBgScanSupported = prop11 ? prop11->supportsBackgroundScanning : false;
    auto jVendorExtension = prop11 ? make_javastr(env, prop11->vendorExension) : nullptr;
    // ITU_1 is the default region just because its index is 0.
    auto jBands = ArrayFromHal<V1_0::BandConfig>(env, prop10.bands, gjni.BandDescriptor.clazz,
        std::bind(BandDescriptorFromHal, _1, _2, Region::ITU_1));
    auto jSupportedProgramTypes =
            prop11 ? ArrayFromHal(env, prop11->supportedProgramTypes) : nullptr;
    auto jSupportedIdentifierTypes =
            prop11 ? ArrayFromHal(env, prop11->supportedIdentifierTypes) : nullptr;

    return make_javaref(env, env->NewObject(gjni.ModuleProperties.clazz,
            gjni.ModuleProperties.cstor, moduleId, jServiceName.get(), prop10.classId,
            jImplementor.get(), jProduct.get(), jVersion.get(), jSerial.get(), prop10.numTuners,
            prop10.numAudioSources, prop10.supportsCapture, jBands.get(), isBgScanSupported,
            jSupportedProgramTypes.get(), jSupportedIdentifierTypes.get(), jVendorExtension.get()));
}

JavaRef<jobject> ModulePropertiesFromHal(JNIEnv *env, const V1_0::Properties &properties,
        jint moduleId, const std::string& serviceName) {
    return ModulePropertiesFromHal(env, properties, nullptr, moduleId, serviceName);
}

JavaRef<jobject> ModulePropertiesFromHal(JNIEnv *env, const V1_1::Properties &properties,
        jint moduleId, const std::string& serviceName) {
    return ModulePropertiesFromHal(env, properties.base, &properties, moduleId, serviceName);
}

static JavaRef<jobject> BandDescriptorFromHal(JNIEnv *env, const V1_0::BandConfig &config, Region region) {
    ALOGV("BandDescriptorFromHal()");

    jint spacing = config.spacings.size() > 0 ? config.spacings[0] : 0;
    ALOGW_IF(config.spacings.size() == 0, "No channel spacing specified");

    switch (config.type) {
        case Band::FM:
        case Band::FM_HD: {
            auto& fm = config.ext.fm;
            return make_javaref(env, env->NewObject(
                    gjni.FmBandDescriptor.clazz, gjni.FmBandDescriptor.cstor,
                    region, config.type, config.lowerLimit, config.upperLimit, spacing,
                    fm.stereo, fm.rds != Rds::NONE, fm.ta, fm.af, fm.ea));
        }
        case Band::AM:
        case Band::AM_HD: {
            auto& am = config.ext.am;
            return make_javaref(env, env->NewObject(
                    gjni.AmBandDescriptor.clazz, gjni.AmBandDescriptor.cstor,
                    region, config.type, config.lowerLimit, config.upperLimit, spacing,
                    am.stereo));
        }
        default:
            ALOGE("Unsupported band type: %d", config.type);
            return nullptr;
    }
}

JavaRef<jobject> BandConfigFromHal(JNIEnv *env, const V1_0::BandConfig &config, Region region) {
    ALOGV("BandConfigFromHal()");

    auto descriptor = BandDescriptorFromHal(env, config, region);
    if (descriptor == nullptr) return nullptr;

    switch (config.type) {
        case Band::FM:
        case Band::FM_HD: {
            return make_javaref(env, env->NewObject(
                    gjni.FmBandConfig.clazz, gjni.FmBandConfig.cstor, descriptor.get()));
        }
        case Band::AM:
        case Band::AM_HD: {
            return make_javaref(env, env->NewObject(
                    gjni.AmBandConfig.clazz, gjni.AmBandConfig.cstor, descriptor.get()));
        }
        default:
            ALOGE("Unsupported band type: %d", config.type);
            return nullptr;
    }
}

V1_0::BandConfig BandConfigToHal(JNIEnv *env, jobject jConfig, Region &region) {
    ALOGV("BandConfigToHal()");
    auto jDescriptor = env->GetObjectField(jConfig, gjni.BandConfig.descriptor);
    if (jDescriptor == nullptr) {
        ALOGE("Descriptor is missing");
        return {};
    }

    region = static_cast<Region>(env->GetIntField(jDescriptor, gjni.BandDescriptor.region));

    V1_0::BandConfig config = {};
    config.type = static_cast<Band>(env->GetIntField(jDescriptor, gjni.BandDescriptor.type));
    config.antennaConnected = false;  // just don't set it
    config.lowerLimit = env->GetIntField(jDescriptor, gjni.BandDescriptor.lowerLimit);
    config.upperLimit = env->GetIntField(jDescriptor, gjni.BandDescriptor.upperLimit);
    config.spacings = hidl_vec<uint32_t>({
        static_cast<uint32_t>(env->GetIntField(jDescriptor, gjni.BandDescriptor.spacing))
    });

    if (env->IsInstanceOf(jConfig, gjni.FmBandConfig.clazz)) {
        auto& fm = config.ext.fm;
        fm.deemphasis = DeemphasisForRegion(region);
        fm.stereo = env->GetBooleanField(jConfig, gjni.FmBandConfig.stereo);
        fm.rds = RdsForRegion(env->GetBooleanField(jConfig, gjni.FmBandConfig.rds), region);
        fm.ta = env->GetBooleanField(jConfig, gjni.FmBandConfig.ta);
        fm.af = env->GetBooleanField(jConfig, gjni.FmBandConfig.af);
        fm.ea = env->GetBooleanField(jConfig, gjni.FmBandConfig.ea);
    } else if (env->IsInstanceOf(jConfig, gjni.AmBandConfig.clazz)) {
        auto& am = config.ext.am;
        am.stereo = env->GetBooleanField(jConfig, gjni.AmBandConfig.stereo);
    } else {
        ALOGE("Unexpected band config type");
        return {};
    }

    return config;
}

Direction DirectionToHal(bool directionDown) {
    return directionDown ? Direction::DOWN : Direction::UP;
}

JavaRef<jobject> MetadataFromHal(JNIEnv *env, const hidl_vec<V1_0::MetaData> &metadata) {
    ALOGV("MetadataFromHal()");
    if (metadata.size() == 0) return nullptr;

    auto jMetadata = make_javaref(env, env->NewObject(
            gjni.RadioMetadata.clazz, gjni.RadioMetadata.cstor));

    for (auto& item : metadata) {
        jint key = static_cast<jint>(item.key);
        jint status = 0;
        switch (item.type) {
            case MetadataType::INT:
                ALOGV("metadata INT %d", key);
                status = env->CallIntMethod(jMetadata.get(), gjni.RadioMetadata.putIntFromNative,
                        key, item.intValue);
                break;
            case MetadataType::TEXT: {
                ALOGV("metadata TEXT %d", key);
                auto value = make_javastr(env, item.stringValue);
                status = env->CallIntMethod(jMetadata.get(), gjni.RadioMetadata.putStringFromNative,
                        key, value.get());
                break;
            }
            case MetadataType::RAW: {
                ALOGV("metadata RAW %d", key);
                auto len = item.rawValue.size();
                if (len == 0) break;
                auto value = make_javaref(env, env->NewByteArray(len));
                if (value == nullptr) {
                    ALOGE("Failed to allocate byte array of len %zu", len);
                    break;
                }
                env->SetByteArrayRegion(value.get(), 0, len,
                        reinterpret_cast<const jbyte*>(item.rawValue.data()));
                status = env->CallIntMethod(jMetadata.get(), gjni.RadioMetadata.putBitmapFromNative,
                        key, value.get());
                break;
            }
            case MetadataType::CLOCK:
                ALOGV("metadata CLOCK %d", key);
                status = env->CallIntMethod(jMetadata.get(), gjni.RadioMetadata.putClockFromNative,
                        key, item.clockValue.utcSecondsSinceEpoch,
                        item.clockValue.timezoneOffsetInMinutes);
                break;
            default:
                ALOGW("invalid metadata type %d", item.type);
        }
        ALOGE_IF(status != 0, "Failed inserting metadata %d (of type %d)", key, item.type);
    }

    return jMetadata;
}

static JavaRef<jobject> ProgramIdentifierFromHal(JNIEnv *env, const ProgramIdentifier &id) {
    ALOGV("ProgramIdentifierFromHal()");
    return make_javaref(env, env->NewObject(gjni.ProgramSelector.Identifier.clazz,
            gjni.ProgramSelector.Identifier.cstor, id.type, id.value));
}

static JavaRef<jobject> ProgramSelectorFromHal(JNIEnv *env, const ProgramSelector &selector) {
    ALOGV("ProgramSelectorFromHal()");
    auto jPrimary = ProgramIdentifierFromHal(env, selector.primaryId);
    auto jSecondary = ArrayFromHal(env, selector.secondaryIds,
            gjni.ProgramSelector.Identifier.clazz, ProgramIdentifierFromHal);
    auto jVendor = ArrayFromHal(env, selector.vendorIds);

    return make_javaref(env, env->NewObject(gjni.ProgramSelector.clazz, gjni.ProgramSelector.cstor,
            selector.programType, jPrimary.get(), jSecondary.get(), jVendor.get()));
}

static ProgramIdentifier ProgramIdentifierToHal(JNIEnv *env, jobject jId) {
    ALOGV("ProgramIdentifierToHal()");

    ProgramIdentifier id = {};
    id.type = env->GetIntField(jId, gjni.ProgramSelector.Identifier.type);
    id.value = env->GetLongField(jId, gjni.ProgramSelector.Identifier.value);
    return id;
}

ProgramSelector ProgramSelectorToHal(JNIEnv *env, jobject jSelector) {
    ALOGV("ProgramSelectorToHal()");

    ProgramSelector selector = {};

    selector.programType = env->GetIntField(jSelector, gjni.ProgramSelector.programType);

    auto jPrimary = env->GetObjectField(jSelector, gjni.ProgramSelector.primaryId);
    auto jSecondary = reinterpret_cast<jobjectArray>(
            env->GetObjectField(jSelector, gjni.ProgramSelector.secondaryIds));
    auto jVendor = reinterpret_cast<jlongArray>(
            env->GetObjectField(jSelector, gjni.ProgramSelector.vendorIds));

    if (jPrimary == nullptr || jSecondary == nullptr || jVendor == nullptr) {
        ALOGE("ProgramSelector object is incomplete");
        return {};
    }

    selector.primaryId = ProgramIdentifierToHal(env, jPrimary);
    auto count = env->GetArrayLength(jSecondary);
    selector.secondaryIds.resize(count);
    for (jsize i = 0; i < count; i++) {
        auto jId = env->GetObjectArrayElement(jSecondary, i);
        selector.secondaryIds[i] = ProgramIdentifierToHal(env, jId);
    }

    count = env->GetArrayLength(jVendor);
    selector.vendorIds.resize(count);
    auto jVendorElements = env->GetLongArrayElements(jVendor, nullptr);
    for (jint i = 0; i < count; i++) {
        selector.vendorIds[i] = jVendorElements[i];
    }
    env->ReleaseLongArrayElements(jVendor, jVendorElements, 0);

    return selector;
}

static JavaRef<jobject> ProgramInfoFromHal(JNIEnv *env, const V1_0::ProgramInfo &info10,
        const V1_1::ProgramInfo *info11, const ProgramSelector &selector) {
    ALOGV("ProgramInfoFromHal()");

    auto jMetadata = MetadataFromHal(env, info10.metadata);
    auto jVendorExtension = info11 ? make_javastr(env, info11->vendorExension) : nullptr;
    auto jSelector = ProgramSelectorFromHal(env, selector);

    return make_javaref(env, env->NewObject(gjni.ProgramInfo.clazz, gjni.ProgramInfo.cstor,
            jSelector.get(), info10.tuned, info10.stereo, info10.digital, info10.signalStrength,
            jMetadata.get(), info11 ? info11->flags : 0, jVendorExtension.get()));
}

JavaRef<jobject> ProgramInfoFromHal(JNIEnv *env, const V1_0::ProgramInfo &info, V1_0::Band band) {
    auto selector = V1_1::utils::make_selector(band, info.channel, info.subChannel);
    return ProgramInfoFromHal(env, info, nullptr, selector);
}

JavaRef<jobject> ProgramInfoFromHal(JNIEnv *env, const V1_1::ProgramInfo &info) {
    return ProgramInfoFromHal(env, info.base, &info, info.selector);
}

} // namespace convert
} // namespace radio
} // namespace server

void register_android_server_radio_convert(JNIEnv *env) {
    using namespace server::radio::convert;

    auto bandConfigClass = FindClassOrDie(env, "android/hardware/radio/RadioManager$BandConfig");
    gjni.BandConfig.descriptor = GetFieldIDOrDie(env, bandConfigClass,
            "mDescriptor", "Landroid/hardware/radio/RadioManager$BandDescriptor;");

    auto fmBandConfigClass = FindClassOrDie(env,
            "android/hardware/radio/RadioManager$FmBandConfig");
    gjni.FmBandConfig.clazz = MakeGlobalRefOrDie(env, fmBandConfigClass);
    gjni.FmBandConfig.cstor = GetMethodIDOrDie(env, fmBandConfigClass,
            "<init>", "(Landroid/hardware/radio/RadioManager$FmBandDescriptor;)V");
    gjni.FmBandConfig.stereo = GetFieldIDOrDie(env, fmBandConfigClass, "mStereo", "Z");
    gjni.FmBandConfig.rds = GetFieldIDOrDie(env, fmBandConfigClass, "mRds", "Z");
    gjni.FmBandConfig.ta = GetFieldIDOrDie(env, fmBandConfigClass, "mTa", "Z");
    gjni.FmBandConfig.af = GetFieldIDOrDie(env, fmBandConfigClass, "mAf", "Z");
    gjni.FmBandConfig.ea = GetFieldIDOrDie(env, fmBandConfigClass, "mEa", "Z");

    auto amBandConfigClass = FindClassOrDie(env,
            "android/hardware/radio/RadioManager$AmBandConfig");
    gjni.AmBandConfig.clazz = MakeGlobalRefOrDie(env, amBandConfigClass);
    gjni.AmBandConfig.cstor = GetMethodIDOrDie(env, amBandConfigClass,
            "<init>", "(Landroid/hardware/radio/RadioManager$AmBandDescriptor;)V");
    gjni.AmBandConfig.stereo = GetFieldIDOrDie(env, amBandConfigClass, "mStereo", "Z");

    auto bandDescriptorClass = FindClassOrDie(env,
            "android/hardware/radio/RadioManager$BandDescriptor");
    gjni.BandDescriptor.clazz = MakeGlobalRefOrDie(env, bandDescriptorClass);
    gjni.BandDescriptor.region = GetFieldIDOrDie(env, bandDescriptorClass, "mRegion", "I");
    gjni.BandDescriptor.type = GetFieldIDOrDie(env, bandDescriptorClass, "mType", "I");
    gjni.BandDescriptor.lowerLimit = GetFieldIDOrDie(env, bandDescriptorClass, "mLowerLimit", "I");
    gjni.BandDescriptor.upperLimit = GetFieldIDOrDie(env, bandDescriptorClass, "mUpperLimit", "I");
    gjni.BandDescriptor.spacing = GetFieldIDOrDie(env, bandDescriptorClass, "mSpacing", "I");

    auto fmBandDescriptorClass = FindClassOrDie(env,
            "android/hardware/radio/RadioManager$FmBandDescriptor");
    gjni.FmBandDescriptor.clazz = MakeGlobalRefOrDie(env, fmBandDescriptorClass);
    gjni.FmBandDescriptor.cstor = GetMethodIDOrDie(env, fmBandDescriptorClass,
            "<init>", "(IIIIIZZZZZ)V");

    auto amBandDescriptorClass = FindClassOrDie(env,
            "android/hardware/radio/RadioManager$AmBandDescriptor");
    gjni.AmBandDescriptor.clazz = MakeGlobalRefOrDie(env, amBandDescriptorClass);
    gjni.AmBandDescriptor.cstor = GetMethodIDOrDie(env, amBandDescriptorClass,
            "<init>", "(IIIIIZ)V");

    auto modulePropertiesClass = FindClassOrDie(env,
            "android/hardware/radio/RadioManager$ModuleProperties");
    gjni.ModuleProperties.clazz = MakeGlobalRefOrDie(env, modulePropertiesClass);
    gjni.ModuleProperties.cstor = GetMethodIDOrDie(env, modulePropertiesClass, "<init>",
            "(ILjava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;"
            "Ljava/lang/String;IIZ[Landroid/hardware/radio/RadioManager$BandDescriptor;Z"
            "[I[ILjava/lang/String;)V");

    auto programInfoClass = FindClassOrDie(env, "android/hardware/radio/RadioManager$ProgramInfo");
    gjni.ProgramInfo.clazz = MakeGlobalRefOrDie(env, programInfoClass);
    gjni.ProgramInfo.cstor = GetMethodIDOrDie(env, programInfoClass, "<init>",
            "(Landroid/hardware/radio/ProgramSelector;ZZZILandroid/hardware/radio/RadioMetadata;I"
            "Ljava/lang/String;)V");

    auto programSelectorClass = FindClassOrDie(env, "android/hardware/radio/ProgramSelector");
    gjni.ProgramSelector.clazz = MakeGlobalRefOrDie(env, programSelectorClass);
    gjni.ProgramSelector.cstor = GetMethodIDOrDie(env, programSelectorClass, "<init>",
            "(ILandroid/hardware/radio/ProgramSelector$Identifier;"
            "[Landroid/hardware/radio/ProgramSelector$Identifier;[J)V");
    gjni.ProgramSelector.programType = GetFieldIDOrDie(env, programSelectorClass,
            "mProgramType", "I");
    gjni.ProgramSelector.primaryId = GetFieldIDOrDie(env, programSelectorClass,
            "mPrimaryId", "Landroid/hardware/radio/ProgramSelector$Identifier;");
    gjni.ProgramSelector.secondaryIds = GetFieldIDOrDie(env, programSelectorClass,
            "mSecondaryIds", "[Landroid/hardware/radio/ProgramSelector$Identifier;");
    gjni.ProgramSelector.vendorIds = GetFieldIDOrDie(env, programSelectorClass,
            "mVendorIds", "[J");

    auto progSelIdClass = FindClassOrDie(env, "android/hardware/radio/ProgramSelector$Identifier");
    gjni.ProgramSelector.Identifier.clazz = MakeGlobalRefOrDie(env, progSelIdClass);
    gjni.ProgramSelector.Identifier.cstor = GetMethodIDOrDie(env, progSelIdClass,
            "<init>", "(IJ)V");
    gjni.ProgramSelector.Identifier.type = GetFieldIDOrDie(env, progSelIdClass,
            "mType", "I");
    gjni.ProgramSelector.Identifier.value = GetFieldIDOrDie(env, progSelIdClass,
            "mValue", "J");

    auto radioMetadataClass = FindClassOrDie(env, "android/hardware/radio/RadioMetadata");
    gjni.RadioMetadata.clazz = MakeGlobalRefOrDie(env, radioMetadataClass);
    gjni.RadioMetadata.cstor = GetMethodIDOrDie(env, radioMetadataClass, "<init>", "()V");
    gjni.RadioMetadata.putIntFromNative = GetMethodIDOrDie(env, radioMetadataClass,
            "putIntFromNative", "(II)I");
    gjni.RadioMetadata.putStringFromNative = GetMethodIDOrDie(env, radioMetadataClass,
            "putStringFromNative", "(ILjava/lang/String;)I");
    gjni.RadioMetadata.putBitmapFromNative = GetMethodIDOrDie(env, radioMetadataClass,
            "putBitmapFromNative", "(I[B)I");
    gjni.RadioMetadata.putClockFromNative = GetMethodIDOrDie(env, radioMetadataClass,
            "putClockFromNative", "(IJI)I");

    auto runtimeExcClass = FindClassOrDie(env, "java/lang/RuntimeException");
    gjni.RuntimeException.clazz = MakeGlobalRefOrDie(env, runtimeExcClass);
    gjni.RuntimeException.cstor = GetMethodIDOrDie(env, runtimeExcClass, "<init>",
            "(Ljava/lang/String;)V");

    auto parcelableExcClass = FindClassOrDie(env, "android/os/ParcelableException");
    gjni.ParcelableException.clazz = MakeGlobalRefOrDie(env, parcelableExcClass);
    gjni.ParcelableException.cstor = GetMethodIDOrDie(env, parcelableExcClass, "<init>",
            "(Ljava/lang/Throwable;)V");
}

} // namespace android
