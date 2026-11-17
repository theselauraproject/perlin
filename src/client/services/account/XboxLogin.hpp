#pragma once
#include "client/services/account/AccountService.hpp"
#include "network/XboxAuth.hpp"

namespace perlin::services {
    class XboxLogin final : public AccountService::Login {
    public:
        explicit XboxLogin(const std::string& authPath) : native(std::make_shared<xbox::Account>(authPath)) {}

        bool HasSavedLogin() const override { return native->HasSavedLogin(); }
        void Cancel() override { native->Cancel(); }
        bool SignIn(const AccountService::Prompt& prompt, std::string& error) override {
            return native->SignIn(prompt, error);
        }
        bool SignInSaved(std::string& error) override { return native->SignInSaved(error); }
        bool FetchGamertag(std::string& gamertag, std::string& error) override {
            return native->FetchGamertag(gamertag, error);
        }

        std::shared_ptr<xbox::Account> Native() const override { return native; }

    private:
        std::shared_ptr<xbox::Account> native;
    };
}
