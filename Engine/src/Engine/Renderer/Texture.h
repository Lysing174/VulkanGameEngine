#pragma once
#include <string>
#include <memory>

namespace Engine {

    class Texture
    {
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual void Bind(uint32_t slot = 0) const = 0;

        // 绑定 ID (用于 ImGui 显示)
        virtual uint32_t GetRendererID() const = 0;

        virtual std::string GetPath() const = 0;

        // 静态创建函数
        static std::shared_ptr<Texture> Create(const std::string& path);
    };

    class Texture2D : public Texture
    {
    public:
        static std::shared_ptr<Texture2D> Create(const std::string& path);
        static std::shared_ptr<Texture2D> GetWhiteTexture();
        static std::shared_ptr<Texture2D> GetBlueTexture();

    private:
        static std::shared_ptr<Texture2D> s_WhiteTexture;
        static std::shared_ptr<Texture2D> s_BlueTexture;

    };
}