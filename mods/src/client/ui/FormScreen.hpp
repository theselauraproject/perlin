#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <string>
#include <memory>
#include <vector>

#include "client/input/InputManager.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiLayout.hpp"

namespace perlin::ui {
    struct FormButton {
        std::string text;
        std::string imageType, imageData, kind;
    };

    struct FormElement {
        enum class Kind { Label, Header, Divider, Input, Toggle, Slider, StepSlider, Dropdown };
        Kind kind = Kind::Label;
        std::string text, placeholder, value;
        bool flag = false;
        double number = 0, min = 0, max = 1, step = 1;
        int index = 0;
        std::vector<std::string> options;
        mutable float trackX = 0, trackW = 0;
    };

    struct FormModel {
        enum class Type { Simple, Modal, Custom };
        uint32_t id = 0;
        Type type = Type::Simple;
        std::string title, content, button1, button2, submit;
        std::vector<FormButton> buttons;
        std::vector<FormElement> elements;
        int focus = -1;
        int openDropdown = -1; // element whose option list is open
    };

    inline constexpr const char* kFormCloseId = "close";

    class FormUi {
    public:
        FormUi();
        ~FormUi();
        void Reset();
        void Warm(const std::vector<std::string>& packDirs);
        bool Ready() const { return state.load() == 2; }
        bool WaitReady(int timeoutMs);
        bool Build(const FormModel& form, int guiW, int guiH, float scroll, std::string& rml,
                   float& contentHeight, float& viewHeight);
        const std::string& Problem() const { return problem; }
        // server ui files still loading
        bool Building() const { return state.load() == 1; }
        // a non-vanilla pack defines server_form
        bool ServerStyled() const {
            return Ready() && library.RootCount() > 3 &&
                   library.DefinedBefore("server_form", library.RootCount() - 3);
        }
        // last form still animating
        bool Animating() const { return anim.animating; }

    private:
        UiLibrary library;
        std::unique_ptr<Node> tree, modalTree;
        RmlEmitter emitter;
        UiServices services;
        AnimHost anim;
        std::unordered_map<std::string, Json> animCache;
        std::chrono::steady_clock::time_point animStart = std::chrono::steady_clock::now();
        std::string problem;
        std::thread worker;
        std::atomic<int> state{0};
        std::string key;
    };

    bool FormImageBytes(const std::string& url, std::string& out);
    uint32_t FormImageVersion();
    bool FormDrag(FormModel& form, int index, float guiX);
    int FormSliderAt(const std::string& hoverId);
    bool ParseForm(uint32_t id, const std::string& json, FormModel& out);
    std::string BuildFormRml(const FormModel& form, int guiW, int guiH, float scroll, float& contentHeight,
                             float& viewHeight);
    bool FormClick(FormModel& form, const std::string& id, std::string& response);
    // an open dropdown takes the next click: true when the click only closed it (dropdown_exit)
    bool FormDismissDropdown(FormModel& form, const std::string& hoverId);
    std::string FormCustomResponse(const FormModel& form);
    bool FormEdit(FormModel& form, const InputManager::Typed& t);
}
