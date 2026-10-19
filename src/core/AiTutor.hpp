#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "core/Lab.hpp"
#include "core/Settings.hpp"

namespace eduvm {

enum class TutorMode {
    Coach,    // Socratic: hints, questions, concepts; no copy-paste solutions
    Explain,  // full explanations with example commands
};

struct TutorContext {
    const Lab* lab = nullptr;
    const LabTask* task = nullptr;
    std::string gradingFeedback;  // failed checks from the last `lab check`
    std::string attachedOutput;   // terminal output / error message the student pasted
    int hintsRevealed = 0;
};

struct TutorReply {
    std::string text;
    bool refused = false;
    bool truncated = false;
    int inputTokens = 0;
    int outputTokens = 0;
};

// A conversation thread persisted in progress/tutor-<key>.json so follow-up
// questions keep their context. Only text is stored, never thinking blocks.
struct TutorConversation {
    std::string key;
    nlohmann::json messages = nlohmann::json::array();    // API turns, including lab context
    nlohmann::json transcript = nlohmann::json::array();  // what the chat panel shows: {role, text}

    static TutorConversation load(const std::string& key);
    void save() const;
    void clear();
};

// AI tutor backed by the Claude Messages API.
class AiTutor {
public:
    explicit AiTutor(Settings settings);

    // Applies the lab's AI policy on top of the student's settings. Throws if
    // the instructor disabled AI for this lab (or exam mode is on).
    static TutorMode effectiveMode(const Settings& settings, const LabAiPolicy* policy);

    TutorReply ask(const std::string& question, const TutorContext& ctx, TutorConversation& conversation) const;

    // Exposed for unit tests.
    static std::string systemPrompt(TutorMode mode);
    static std::string labContextBlock(const TutorContext& ctx);
    static std::string userTurn(const std::string& question, const TutorContext& ctx);
    nlohmann::json buildRequest(TutorMode mode, const TutorContext& ctx, const nlohmann::json& messages) const;
    static TutorReply parseResponse(int httpStatus, const std::string& body);

private:
    Settings settings_;
};

}  // namespace eduvm
