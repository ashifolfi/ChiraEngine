#pragma once

#include "angelscriptProvider.h"

namespace chira {
    class AngelscriptProvider;

    class AngelscriptHolder {
    public:
        explicit AngelscriptHolder(const std::string& path);
        virtual ~AngelscriptHolder();
        void init(AngelscriptProvider* provider);
        void render(AngelscriptProvider* provider, double delta);
        void stop(AngelscriptProvider* provider);
        [[nodiscard]] std::string getIdentifier() const {
            return this->identifier;
        }
    private:
        std::string identifier;
        asIScriptContext* scriptContext = nullptr;
        asIScriptFunction* initFunc = nullptr;
        asIScriptFunction* renderFunc = nullptr;
        asIScriptFunction* stopFunc = nullptr;
    };
}
