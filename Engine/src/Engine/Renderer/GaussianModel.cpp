#include "pch.h"
#include "GaussianModel.h"

#include <happly.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Engine {
    namespace {

        // Reference interleaved binary records or happly's property arrays.
        // Only non-float properties need a converted copy.
        struct FloatProperty
        {
            const float* data = nullptr;
            size_t stride = 1;
            std::vector<float> converted;

            float operator[](size_t index) const
            {
                return converted.empty() ? data[index * stride] : converted[index];
            }
        };

        template<typename HasProperty, typename GetProperty>
        void ConvertGaussians(size_t count, HasProperty hasProperty, GetProperty getProperty,
            std::vector<GaussianDataGPU>& gaussians)
        {
            if (count > std::numeric_limits<uint32_t>::max() || count > gaussians.max_size())
                throw std::runtime_error("Gaussian count exceeds the supported array size");

            std::array<FloatProperty, 3> position = { getProperty("x"), getProperty("y"), getProperty("z") };
            const bool hasDC = hasProperty("f_dc_0");
            const bool hasScale = hasProperty("scale_0");
            const bool hasRotation = hasProperty("rot_0");
            const bool hasOpacity = hasProperty("opacity");
            std::array<FloatProperty, 3> dc, scale;
            std::array<FloatProperty, 4> rotation;
            FloatProperty opacity;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                if (hasDC) dc[channel] = getProperty("f_dc_" + std::to_string(channel));
                if (hasScale) scale[channel] = getProperty("scale_" + std::to_string(channel));
            }
            if (hasRotation)
                for (size_t component = 0; component < 4; ++component)
                    rotation[component] = getProperty("rot_" + std::to_string(component));
            if (hasOpacity) opacity = getProperty("opacity");

            size_t restCount = 0;
            while (hasProperty("f_rest_" + std::to_string(restCount))) ++restCount;
            if (restCount % 3 != 0)
                throw std::runtime_error("PLY SH coefficient count must be divisible by 3");
            const size_t restPerChannel = restCount / 3;
            const size_t restToLoad = std::min(restPerChannel, size_t(SH_BASIS - 1));
            std::array<FloatProperty, SH_TOTAL - 3> rest;
            for (size_t basis = 0; basis < restToLoad; ++basis)
                for (size_t channel = 0; channel < 3; ++channel)
                    rest[3 * basis + channel] = getProperty("f_rest_" + std::to_string(channel * restPerChannel + basis));

            gaussians.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                GaussianDataGPU& out = gaussians[i];
                const float rawOpacity = hasOpacity ? opacity[i] : 0.0f;
                out.posAndOpacity = glm::vec4(position[0][i], position[1][i], position[2][i],
                    1.0f / (1.0f + std::exp(-rawOpacity)));
                std::fill(std::begin(out.sh), std::end(out.sh), 0.0f);
                if (hasDC)
                    for (size_t channel = 0; channel < 3; ++channel)
                        out.sh[channel] = dc[channel][i];
                for (size_t basis = 0; basis < restToLoad; ++basis)
                    for (size_t channel = 0; channel < 3; ++channel)
                        out.sh[3 * (basis + 1) + channel] = rest[3 * basis + channel][i];

                const glm::vec3 activatedScale = hasScale
                    ? glm::vec3(std::exp(scale[0][i]), std::exp(scale[1][i]), std::exp(scale[2][i]))
                    : glm::vec3(1.0f);
                const glm::quat normalizedRotation = hasRotation
                    ? glm::normalize(glm::quat(rotation[0][i], rotation[1][i], rotation[2][i], rotation[3][i]))
                    : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                const glm::mat3 S = glm::mat3(glm::scale(glm::mat4(1.0f), activatedScale));
                const glm::mat3 R = glm::mat3_cast(normalizedRotation);
                const glm::mat3 M = R * S;
                const glm::mat3 covariance = M * glm::transpose(M);
                out.cov3d[0] = covariance[0][0];
                out.cov3d[1] = covariance[0][1];
                out.cov3d[2] = covariance[0][2];
                out.cov3d[3] = covariance[1][1];
                out.cov3d[4] = covariance[1][2];
                out.cov3d[5] = covariance[2][2];
                out.modelIndex = 0;
                out._pad1 = 0.0f;
            }
        }

        bool TryLoadBinaryPLY(const std::string& path, std::vector<GaussianDataGPU>& gaussians)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open Gaussian PLY file");

            std::string line;
            std::getline(input, line);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line != "ply") throw std::runtime_error("Invalid PLY signature");

            const uint16_t endianCheck = 1;
            bool supported = *reinterpret_cast<const unsigned char*>(&endianCheck) == 1;
            bool binaryFormat = false;
            bool firstElement = true;
            bool vertexFirst = false;
            bool inVertex = false;
            bool headerComplete = false;
            uint64_t count = 0;
            std::vector<std::string> properties;
            while (std::getline(input, line))
            {
                std::istringstream tokens(line);
                std::string keyword;
                tokens >> keyword;
                if (keyword == "end_header") { headerComplete = true; break; }
                if (keyword == "format")
                {
                    std::string format, version;
                    tokens >> format >> version;
                    binaryFormat = format == "binary_little_endian" && version == "1.0";
                }
                else if (keyword == "element")
                {
                    std::string name;
                    tokens >> name;
                    inVertex = name == "vertex";
                    if (inVertex)
                    {
                        if (!(tokens >> count) || count > std::numeric_limits<uint32_t>::max())
                            throw std::runtime_error("Invalid or unsupported PLY vertex count");
                        vertexFirst = firstElement;
                    }
                    firstElement = false;
                }
                else if (keyword == "property" && inVertex)
                {
                    std::string type, name;
                    tokens >> type >> name;
                    supported = supported && (type == "float" || type == "float32") && !name.empty();
                    properties.push_back(name);
                }
            }
            if (!headerComplete) throw std::runtime_error("PLY header is incomplete");
            if (!supported || !binaryFormat || !vertexFirst) return false;

            std::unordered_map<std::string, size_t> offsets;
            for (size_t i = 0; i < properties.size(); ++i)
                if (!offsets.emplace(properties[i], i).second)
                    throw std::runtime_error("Duplicate PLY vertex property");
            const size_t stride = properties.size();
            if (stride == 0 || count > std::vector<float>().max_size() / stride ||
                count > uint64_t(std::numeric_limits<std::streamsize>::max()) / sizeof(float) / stride)
                throw std::runtime_error("PLY vertex data size is invalid or too large");
            const size_t floatCount = size_t(count) * stride;
            const std::streamsize bytes = std::streamsize(floatCount * sizeof(float));
            const std::streampos dataStart = input.tellg();
            input.seekg(0, std::ios::end);
            const std::streampos fileEnd = input.tellg();
            if (dataStart == std::streampos(-1) || fileEnd == std::streampos(-1) || fileEnd - dataStart < bytes)
                throw std::runtime_error("PLY vertex data is truncated");
            input.seekg(dataStart);

            // Read interleaved records in one operation. Header offsets support
            // reordered fields, omitted normals and lower SH degrees as well.
            std::vector<float> records(floatCount);
            if (bytes != 0 && !input.read(reinterpret_cast<char*>(records.data()), bytes))
                throw std::runtime_error("Failed to read complete PLY vertex data");
            auto hasProperty = [&](const std::string& name) { return offsets.count(name) != 0; };
            auto getProperty = [&](const std::string& name) {
                const auto property = offsets.find(name);
                if (property == offsets.end()) throw std::runtime_error("Missing PLY property " + name);
                FloatProperty view;
                view.data = records.empty() ? nullptr : records.data() + property->second;
                view.stride = stride;
                return view;
            };
            ConvertGaussians(size_t(count), hasProperty, getProperty, gaussians);
            return true; // records is destroyed before the caller starts uploading.
        }

        void LoadCompatiblePLY(const std::string& path, std::vector<GaussianDataGPU>& gaussians)
        {
            std::ifstream input(path, std::ios::binary);
            input.exceptions(std::ios::badbit | std::ios::failbit);
            happly::PLYData plyFile(input);
            auto& vertex = plyFile.getElement("vertex");
            auto hasProperty = [&](const std::string& name) { return vertex.hasProperty(name); };
            auto getProperty = [&](const std::string& name) {
                FloatProperty view;
                auto* floats = dynamic_cast<happly::TypedProperty<float>*>(vertex.getPropertyPtr(name).get());
                if (floats)
                {
                    if (floats->data.size() != vertex.count) throw std::runtime_error("Invalid PLY property length");
                    view.data = floats->data.data();
                }
                else
                {
                    view.converted = vertex.getProperty<float>(name);
                    if (view.converted.size() != vertex.count) throw std::runtime_error("Invalid PLY property length");
                }
                return view;
            };
            ConvertGaussians(vertex.count, hasProperty, getProperty, gaussians);
        }
    }

    std::shared_ptr<GaussianModel> GaussianModel::Create(const std::string& path)
    {
        try
        {
            return std::shared_ptr<GaussianModel>(new GaussianModel(path));
        }
        catch (const std::exception& e)
        {
            EG_CORE_ERROR("Failed to load Gaussian model '{0}': {1}", path, e.what());
            return nullptr;
        }
    }

    GaussianModel::GaussianModel(const std::string& path)
    {
        LoadModel(path);
    }

    void GaussianModel::LoadModel(const std::string& path)
    {
        const bool bulkRead = TryLoadBinaryPLY(path, m_Gaussians);
        if (!bulkRead) LoadCompatiblePLY(path, m_Gaussians);
        EG_CORE_INFO("Loaded Gaussian model: {0} ({1} gaussians, {2})", path, m_Gaussians.size(),
            bulkRead ? "bulk binary read" : "compatible PLY reader");
    }

    void GaussianModel::CopyGPUData(GaussianDataGPU* destination, uint32_t modelIndex) const
    {
        if (m_Gaussians.empty()) return;
        if (!destination) throw std::invalid_argument("Gaussian staging destination is null");
        std::memcpy(destination, m_Gaussians.data(), m_Gaussians.size() * sizeof(GaussianDataGPU));
        // The cache's zero placeholder already matches the first instance.
        if (modelIndex != 0)
            for (size_t i = 0; i < m_Gaussians.size(); ++i)
                destination[i].modelIndex = modelIndex;
    }
}
