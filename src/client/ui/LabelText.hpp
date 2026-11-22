#pragma once

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace perlin::ui {

    // Splits one paragraph of label text into lines no wider than wrapWidth (no limit when it is not
    // positive). Leading and doubled spaces stay in the text the way Bedrock draws them; only the
    // single space a wrap breaks on is dropped. `measure` returns the width of a string in GUI px.
    inline std::vector<std::string> WrapLabelParagraph(const std::string& paragraph, float wrapWidth,
                                                       const std::function<float(const std::string&)>& measure) {
        std::vector<std::string> out;
        std::string line;
        bool started = false; // whether the line has any word yet; an empty first word is a leading space
        size_t i = 0;
        while (i <= paragraph.size()) {
            const size_t sp = paragraph.find(' ', i);
            const std::string word = paragraph.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            const std::string candidate = started ? line + " " + word : word;
            if (started && wrapWidth > 0 && !line.empty() && measure(candidate) > wrapWidth) {
                out.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
            started = true;
            if (sp == std::string::npos)
                break;
            i = sp + 1;
        }
        out.push_back(line);
        return out;
    }

    // Splits label text on newlines and wraps each paragraph.
    inline std::vector<std::string> WrapLabelText(const std::string& text, float wrapWidth,
                                                  const std::function<float(const std::string&)>& measure) {
        std::vector<std::string> out;
        size_t start = 0;
        while (start <= text.size()) {
            const size_t nl = text.find('\n', start);
            const std::string paragraph = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            for (std::string& l : WrapLabelParagraph(paragraph, wrapWidth, measure))
                out.push_back(std::move(l));
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
        return out;
    }

    // Width a "default"-sized bitmap-font label takes. Bedrock keeps one pixel after the text for the
    // drop shadow whether or not the label draws one, so a box sized "100%c + n" around a label is
    // one pixel wider than the glyph advances alone. Without it, text padded with a leading space
    // (as server scoreboards do) sits a pixel right of centre.
    inline float LabelDefaultWidth(float measured, bool bitmapFont) {
        if (measured <= 0)
            return 0;
        return std::ceil(measured) + (bitmapFont ? 1.0f : 0.0f);
    }
}
