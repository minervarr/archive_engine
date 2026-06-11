#include "ae/tag.hh"

#include <chrono>
#include <thread>

#include <taglib/fileref.h>
#include <taglib/tbytevector.h>
#include <taglib/tpropertymap.h>
#include <taglib/tvariant.h>

#include "ae/log.hh"

namespace ae {

namespace {

Error tag_error(std::string message) {
    AE_LOGE("%s", message.c_str());
    return Error{ErrorKind::Tagging, 0, std::move(message)};
}

} // namespace

Result<void> write_tags(const std::string &path, const TagData &tags) {
    TagLib::FileRef file(path.c_str());
    if (file.isNull() || !file.file() || !file.file()->isValid()) {
        return tag_error("cannot open audio file for tagging: " + path);
    }

    TagLib::PropertyMap props;
    if (!tags.clear_existing) props = file.file()->properties();
    for (const auto &[key, value] : tags.fields) {
        props[TagLib::String(key, TagLib::String::UTF8)].append(
            TagLib::String(value, TagLib::String::UTF8));
    }
    file.file()->setProperties(props);

    if (tags.clear_existing || tags.cover) {
        TagLib::List<TagLib::VariantMap> pictures;
        if (tags.cover) {
            TagLib::VariantMap picture;
            picture["data"] = TagLib::ByteVector(
                reinterpret_cast<const char *>(tags.cover->data.data()),
                static_cast<unsigned int>(tags.cover->data.size()));
            picture["mimeType"] = TagLib::String(tags.cover->mime_type);
            picture["pictureType"] = TagLib::String("Front Cover");
            pictures.append(picture);
        }
        file.file()->setComplexProperties("PICTURE", pictures);
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (file.save()) return {};
        if (attempt < 2) {
            AE_LOGD("tag save failed for %s, retrying (attempt %d)", path.c_str(), attempt + 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(250 * (attempt + 1)));
        }
    }
    return tag_error("failed to save tags after 3 attempts: " + path);
}

} // namespace ae
