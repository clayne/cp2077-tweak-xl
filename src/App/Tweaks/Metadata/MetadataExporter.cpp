#include "MetadataExporter.hpp"
#include "App/Tweaks/Declarative/Red/RedReader.hpp"
#include "Red/TweakDB/Manager.hpp"
#include "Red/TweakDB/Source/Parser.hpp"

namespace
{
constexpr auto TweakExtension = Red::TweakSource::Extension;
constexpr auto SchemaPackage = Red::TweakSource::SchemaPackage;
constexpr auto NameSeparator = Red::TweakGrammar::Name::Separator;
constexpr auto InlineSuffix = "_inline";
constexpr auto DebugTag = "Debug";
}

App::MetadataExporter::MetadataExporter(Core::SharedPtr<Red::TweakDBManager> aManager)
    : m_manager(std::move(aManager))
    , m_resolved(true)
{
}

bool App::MetadataExporter::LoadSource(const std::filesystem::path& aSourceDir)
{
    std::error_code error;

    if (std::filesystem::exists(aSourceDir, error))
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(aSourceDir, error))
        {
            if (entry.is_regular_file() && entry.path().extension() == TweakExtension)
            {
                m_sources.push_back(Red::TweakParser::Parse(entry.path()));
                m_resolved = false;
            }
        }
    }

    return !m_sources.empty();
}

bool App::MetadataExporter::IsDebugGroup(const Red::TweakGroupPtr& aGroup)
{
    return std::any_of(aGroup->tags.begin(), aGroup->tags.end(), [](auto& aTag) {
        return aTag == DebugTag;
    });
}

void App::MetadataExporter::ResolveGroups()
{
    if (m_resolved)
        return;

    m_groups.clear();

    Core::Map<std::string, Core::Map<std::string, Red::TweakGroupPtr>> bases;

    for (auto& source : m_sources)
    {
        for (auto& group : source->groups)
        {
            bases[source->package][group->name] = group;

            if (source->isPackage)
            {
                group->name = source->package + NameSeparator + group->name;
            }

            m_groups[group->name] = group;
        }
    }

    for (auto& source : m_sources)
    {
        Core::Vector<Red::TweakGroupPtr> groups;
        groups.reserve(source->groups.size() + source->inlines.size());
        groups.insert(groups.end(), source->groups.begin(), source->groups.end());
        for (auto& inlined : source->inlines)
        {
            groups.push_back(inlined->group);
        }

        for (auto& group : groups)
        {
            if (group->base.empty() || m_groups.contains(group->base))
                continue;

            if (bases[source->package].contains(group->base))
            {
                group->base = source->package + NameSeparator + group->base;
            }
            else
            {
                for (auto& package : source->usings)
                {
                    if (bases[package].contains(group->base))
                    {
                        group->base = package + NameSeparator + group->base;
                        break;
                    }
                }
            }
        }
    }

    for (auto& source : m_sources)
    {
        for (auto& group : source->groups)
        {
            ResolveInlines(group, group);
        }
    }

    for (auto& [_, group] : m_groups)
    {
        if (group->base.empty() || group->isSchema || group->isQuery || IsDebugGroup(group))
            continue;

        auto parent = m_groups[group->base];
        while (parent)
        {
            if (parent->isSchema)
            {
                m_records[group->name] = parent->name;
                break;
            }

            parent = !parent->base.empty() ? m_groups[parent->base] : nullptr;
        }
    }

    m_resolved = true;
}

void App::MetadataExporter::ResolveInlines(const Red::TweakGroupPtr& aOwner, const Red::TweakGroupPtr& aParent,
                                           int32_t aCounter)
{
    auto inlineBaseName = aOwner->name + InlineSuffix;

    for (const auto& flat : aParent->flats)
    {
        auto counter = aCounter;
        auto offset = 0u;
        Red::Value<> flatValue;

        for (auto i = 0; i < flat->values.size(); ++i)
        {
            auto& value = flat->values[i];
            auto& group = value->group;

            if (value->type != Red::ETweakValueType::Inline)
                continue;

            if (!flatValue)
            {
                auto flatName = aParent->name + NameSeparator + flat->name;
                auto flatID = Red::TweakDBID(flatName);

                flatValue = m_manager->GetFlat(flatID);

                if (!flatValue)
                {
                    LogWarning("Can't resolve inline name for {}: flat doesn't exist.", flatName);
                    break;
                }

                if (flat->isArray && flat->operation == Red::ETweakFlatOp::Append && !aParent->base.empty())
                {
                    auto inheritedFlatName = aParent->base + NameSeparator + flat->name;
                    auto inheritedFlatID = Red::TweakDBID(inheritedFlatName);
                    auto inheritedFlatValue = m_manager->GetFlat(inheritedFlatID);

                    if (!inheritedFlatValue)
                    {
                        LogWarning("Can't resolve inline name for {}: inherited flat doesn't exist.", flatName);
                        break;
                    }

                    offset = inheritedFlatValue.As<Red::DynArray<Red::TweakDBID>>().size;
                }
            }

            auto resolvedID = flat->isArray
                ? flatValue.As<Red::DynArray<Red::TweakDBID>>()[offset + i]
                : flatValue.As<Red::TweakDBID>();

#ifndef NDEBUG
            auto resolvedName = m_reflection->ToString(resolvedID);
#endif

            while (++counter < 999)
            {
                auto inlineName = inlineBaseName + std::to_string(counter);
                auto inlineID = Red::TweakDBID(inlineName);

                if (inlineID == resolvedID)
                {
                    group->name = inlineName;
                    m_groups[group->name] = group;
                    break;
                }
            }

            if (group->name.empty())
            {
                auto flatName = aParent->name + NameSeparator + flat->name;
                if (flat->isArray)
                {
                    flatName += '[';
                    flatName += std::to_string(i);
                    flatName += ']';
                }

                LogWarning("Can't resolve inline name for {}: no matches found.", flatName);
                break;
            }

            ResolveInlines(aOwner, group, counter);
        }
    }
}

bool App::MetadataExporter::ExportInheritanceMap(const std::filesystem::path& aOutPath, bool aGeneratedComment)
{
    ResolveGroups();

    if (m_records.empty())
        return false;

    Core::Map<std::string, Core::Set<std::string>> map;

    for (auto& [recordName, schemaName] : m_records)
    {
        auto& record = m_groups[recordName];
        if (record->base != schemaName)
        {
            map[record->base].insert(record->name);
        }
    }

    if (aOutPath.extension() == ".dat")
    {
        std::ofstream out(aOutPath, std::ios::binary);

        auto numberOfEntries = map.size();
        out.write(reinterpret_cast<char*>(&numberOfEntries), sizeof(numberOfEntries));

        for (const auto& [recordName, childNames] : map)
        {
            auto recordID = Red::TweakDBID(recordName);
            auto numberOfChildren = childNames.size();

            out.write(reinterpret_cast<char*>(&recordID), sizeof(recordID));
            out.write(reinterpret_cast<char*>(&numberOfChildren), sizeof(numberOfChildren));

            for (const auto& childName : childNames)
            {
                auto childID = Red::TweakDBID(childName);

                out.write(reinterpret_cast<char*>(&childID), sizeof(childID));
            }
        }
    }
    else if (aOutPath.extension() == ".yaml")
    {
        std::ofstream out(aOutPath, std::ios::out);

        if (aGeneratedComment)
        {
            out << "# GENERATED FROM REDMOD SOURCES" << std::endl;
            out << "# DO NOT EDIT BY HAND" << std::endl;
        }

        for (const auto& [recordName, childNames] : map)
        {
            out << recordName << ":" << std::endl;

            for (const auto& childName : childNames)
            {
                out << "- " << childName << std::endl;
            }
        }
    }

    return true;
}

bool App::MetadataExporter::ExportExtraFlats(const std::filesystem::path& aOutPath, bool aGeneratedComment)
{
    ResolveGroups();

    if (m_records.empty())
        return false;

    Core::Map<std::string, Core::Map<std::string, Red::TweakFlatPtr>> extras;

    for (auto& [recordName, schemaName] : m_records)
    {
        auto& record = m_groups[recordName];

        for (auto& flat : record->flats)
        {
            if (flat->type == Red::ETweakFlatType::Undefined)
                continue;

            if (extras.contains(schemaName) && extras[schemaName].contains(flat->name))
                continue;

            auto schema = m_groups[schemaName];
            while (schema)
            {
                auto found = std::ranges::any_of(schema->flats, [&flat](auto& aProp) {
                    return aProp->name == flat->name;
                });

                if (found)
                    break;

                schema = !schema->base.empty() ? m_groups[schema->base] : nullptr;
            }

            if (!schema)
            {
                extras[schemaName][flat->name] = flat;
            }
        }
    }

    if (aOutPath.extension() == ".dat")
    {
        std::ofstream out(aOutPath, std::ios::binary);

        auto numberOfEntries = extras.size();
        out.write(reinterpret_cast<char*>(&numberOfEntries), sizeof(numberOfEntries));

        for (const auto& [schemaName, extraFlats] : extras)
        {
            std::string_view typeName = schemaName;
            typeName.remove_prefix(std::char_traits<char>::length(SchemaPackage) + 1);

            auto recordType = m_reflection->GetRecordFullName(typeName.data());
            auto numberOfFlats = extraFlats.size();

            out.write(reinterpret_cast<char*>(&recordType), sizeof(recordType));
            out.write(reinterpret_cast<char*>(&numberOfFlats), sizeof(numberOfFlats));

            for (const auto& [_, flat] : extraFlats)
            {
                uint8_t flatNameLen = flat->name.size();
                auto flatName = flat->name.data();
                auto flatType = RedReader::GetFlatTypeName(flat);
                auto foreignType = m_reflection->GetRecordFullName(flat->foreignType.data());

                out.write(reinterpret_cast<char*>(&flatNameLen), sizeof(flatNameLen));
                out.write(reinterpret_cast<char*>(flatName), flatNameLen);
                out.write(reinterpret_cast<char*>(&flatType), sizeof(flatType));
                out.write(reinterpret_cast<char*>(&foreignType), sizeof(foreignType));
            }
        }
    }
    else if (aOutPath.extension() == ".yaml")
    {
        std::ofstream out(aOutPath, std::ios::out);

        if (aGeneratedComment)
        {
            out << "# GENERATED FROM REDMOD SOURCES" << std::endl;
            out << "# DO NOT EDIT BY HAND" << std::endl;
        }

        for (const auto& [schemaName, extraFlats] : extras)
        {
            std::string_view typeName = schemaName;
            typeName.remove_prefix(std::char_traits<char>::length(SchemaPackage) + 1);

            out << typeName << ":" << std::endl;

            for (const auto& [_, flat] : extraFlats)
            {
                out << "  " << flat->name << ":" << std::endl;
                out << "    flatType: " << RedReader::GetFlatTypeName(flat).ToString() << std::endl;

                if (!flat->foreignType.empty())
                {
                    out << "    foreignType: " << flat->foreignType << std::endl;
                }
            }
        }
    }

    return true;
}
