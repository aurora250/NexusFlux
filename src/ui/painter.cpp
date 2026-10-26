#include <NeFlux/common.hpp>
#include <NeFlux/model.hpp>
#include <NeFlux/ui/painter.hpp>
#include <NeForce/core/string/format.hpp>
namespace neforce::neflux::ui {

    namespace {

        tui::style make_style(color fg, optional<color> bg = none, bool bold = false) {
            tui::style value;
            value.fg = fg;
            if (bg.has_value()) {
                value.bg = *bg;
            }
            if (bold) {
                value.bold = true;
            }
            return value;
        }

        color blend(const color& base, const color& accent) {
            const auto channel = [](int from, int to) { return (from + to * 4) / 5; };
            return color{channel(base.R(), accent.R()), channel(base.G(), accent.G()), channel(base.B(), accent.B())};
        }

        bool light_palette(const palette& p) { return p.panel_.R() + p.panel_.G() + p.panel_.B() > 540; }

        bool is_monochrome(const palette& p) {
            return p.mint_.R() == p.text_.R() && p.mint_.G() == p.text_.G() && p.mint_.B() == p.text_.B() &&
                   p.blue_.R() == p.text_.R() && p.blue_.G() == p.text_.G() && p.blue_.B() == p.text_.B();
        }

        color cpu_color(double level, const palette& p) {
            if (is_monochrome(p)) {
                const bool light = light_palette(p);
                const int base = light ? 40 : 120;
                const int span = light ? 120 : 120;
                const int value = base + static_cast<int>(neforce::clamp(level / 100.0, 0.0, 1.0) * span);
                return color{value, value, value};
            }
            const bool light = light_palette(p);
            const color green = light ? color{0, 125, 72} : color{119, 202, 155};
            const color yellow = light ? color{162, 124, 0} : color{203, 192, 108};
            const color red = light ? color{187, 39, 39} : color{220, 76, 76};
            level = neforce::clamp(level, 0.0, 1.0);
            const color start = level <= 0.5 ? green : yellow;
            const color end = level <= 0.5 ? yellow : red;
            const double fraction = level <= 0.5 ? level * 2.0 : (level - 0.5) * 2.0;
            const auto mix = [fraction](int a, int b) {
                return static_cast<int>(
                        neforce::round(static_cast<double>(a) + (static_cast<double>(b) - a) * fraction));
            };
            return color{mix(start.R(), end.R()), mix(start.G(), end.G()), mix(start.B(), end.B())};
        }

        codepoint first_codepoint(string_view text) {
            if (text.empty()) {
                return codepoint(U' ');
            }
            size_t index = 0;
            return codepoint::decode_utf8(reinterpret_cast<const byte_t*>(text.data()), index, text.size());
        }

        void line(int x, int y, int end_x, int end_y, const function<void(int, int)>& dot) {
            const int dx = neforce::absolute(end_x - x);
            const int dy = -neforce::absolute(end_y - y);
            const int sx = x < end_x ? 1 : -1;
            const int sy = y < end_y ? 1 : -1;
            int error = dx + dy;
            for (;;) {
                dot(x, y);
                if (x == end_x && y == end_y) {
                    break;
                }
                const int twice = 2 * error;
                if (twice >= dy) {
                    error += dy;
                    x += sx;
                }
                if (twice <= dx) {
                    error += dx;
                    y += sy;
                }
            }
        }

    } // namespace

    palette make_palette(const string& name, const json& custom) {
        palette p{};
        if (name == "black") {
            p = {color{0, 0, 0},       color{10, 10, 10},    color{55, 55, 60},    color{160, 166, 176},
                 color{235, 238, 243}, color{105, 205, 171}, color{124, 179, 239}, color{189, 163, 228},
                 color{226, 186, 111}, color{239, 146, 154}, color{25, 41, 48}};
        } else if (name == "white") {
            p = {color{245, 247, 250}, color{255, 255, 255}, color{173, 184, 198}, color{82, 96, 114},
                 color{28, 39, 55},    color{0, 109, 88},    color{32, 87, 162},   color{112, 64, 159},
                 color{132, 84, 14},   color{174, 42, 59},   color{224, 234, 246}};
        } else if (name == "graphite") {
            p = {color{22, 22, 24},    color{29, 29, 32},    color{63, 63, 70},    color{161, 161, 170},
                 color{235, 235, 239}, color{163, 205, 184}, color{151, 180, 214}, color{191, 174, 215},
                 color{213, 189, 143}, color{224, 158, 166}, color{49, 51, 58}};
        } else if (name == "ocean") {
            p = {color{8, 24, 36},     color{12, 32, 48},    color{41, 71, 91},    color{143, 173, 190},
                 color{219, 235, 245}, color{103, 197, 187}, color{116, 183, 230}, color{175, 166, 220},
                 color{218, 189, 137}, color{232, 154, 169}, color{22, 58, 79}};
        } else if (name == "forest") {
            p = {color{13, 24, 19},    color{18, 32, 25},    color{48, 73, 58},    color{151, 177, 157},
                 color{224, 237, 223}, color{151, 200, 143}, color{141, 184, 204}, color{187, 165, 198},
                 color{216, 188, 133}, color{224, 158, 151}, color{36, 57, 42}};
        } else if (name == "sand") {
            p = {color{244, 237, 224}, color{253, 248, 238}, color{181, 165, 144}, color{108, 94, 76},
                 color{58, 45, 32},    color{51, 105, 76},   color{48, 94, 139},   color{121, 73, 131},
                 color{135, 86, 25},   color{171, 53, 48},   color{232, 219, 196}};
        } else if (name == "nord") {
            p = {color{29, 35, 46},    color{36, 44, 57},    color{68, 81, 101},   color{160, 174, 193},
                 color{229, 233, 240}, color{145, 181, 178}, color{143, 181, 195}, color{190, 168, 186},
                 color{204, 184, 143}, color{214, 159, 167}, color{52, 66, 84}};
        } else if (name == "ember") {
            p = {color{22, 18, 24},    color{29, 24, 33},    color{73, 55, 70},    color{176, 155, 172},
                 color{240, 222, 227}, color{204, 165, 143}, color{140, 173, 202}, color{180, 156, 203},
                 color{207, 184, 138}, color{215, 151, 161}, color{65, 43, 53}};
        } else {
            p = {color{10, 15, 25},    color{14, 21, 34},    color{42, 58, 80},    color{143, 160, 182},
                 color{215, 226, 241}, color{124, 184, 165}, color{134, 166, 204}, color{173, 157, 198},
                 color{204, 179, 141}, color{216, 151, 160}, color{29, 51, 70}};
        }
        for (const auto& member: custom.as_object()) {
            if (!member.second.is_string() || member.second.as_string().size() != 7) {
                continue;
            }
            const string& hex = member.second.as_string();
            const auto parse_hex = [&hex](size_t offset) {
                int value = 0;
                for (size_t index = offset; index < offset + 2; ++index) {
                    const char c = hex[index];
                    const int digit =
                            c >= '0' && c <= '9'
                                    ? c - '0'
                                    : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0));
                    value = value * 16 + digit;
                }
                return value;
            };
            const color value{parse_hex(1), parse_hex(3), parse_hex(5)};
            if (member.first == "background") {
                p.bg_ = value;
            } else if (member.first == "panel") {
                p.panel_ = value;
            } else if (member.first == "text") {
                p.text_ = value;
            } else if (member.first == "accent") {
                p.mint_ = value;
            } else if (member.first == "border") {
                p.border_ = value;
            } else if (member.first == "muted") {
                p.muted_ = value;
            } else if (member.first == "selection") {
                p.selection_ = value;
            } else if (member.first == "blue") {
                p.blue_ = value;
            } else if (member.first == "purple") {
                p.purple_ = value;
            } else if (member.first == "amber") {
                p.amber_ = value;
            } else if (member.first == "red") {
                p.red_ = value;
            }
        }
        return p;
    }

    palette monochrome_palette(const palette& source) {
        palette colors = source;
        const auto gray = [](const color& value) {
            const int level = (value.R() * 299 + value.G() * 587 + value.B() * 114) / 1000;
            return color{level, level, level};
        };
        colors.bg_ = gray(source.bg_);
        colors.panel_ = gray(source.panel_);
        colors.border_ = gray(source.border_);
        colors.muted_ = gray(source.muted_);
        colors.text_ = gray(source.text_);
        colors.mint_ = colors.text_;
        colors.blue_ = colors.text_;
        colors.purple_ = colors.text_;
        colors.amber_ = colors.text_;
        colors.red_ = colors.text_;
        colors.selection_ = gray(source.selection_);
        return colors;
    }

    color state_color(const string& value, const palette& p) {
        string lower = lower_ascii(value.view());
        if (lower == "running" || lower == "active" || lower == "up" || lower == "established" || lower == "run" ||
            lower == "runnable" || lower == "healthy") {
            return p.mint_;
        }
        if (lower == "failed" || lower == "dead" || lower == "unhealthy" || lower == "zombie") {
            return p.red_;
        }
        if (lower == "restarting" || lower == "activating" || lower == "deactivating" || lower == "time_wait" ||
            lower == "close_wait" || lower == "paused" || lower == "idle") {
            return p.amber_;
        }
        if (lower == "listen" || lower == "listening") {
            return p.blue_;
        }
        return p.muted_;
    }

    painter::painter(tui::canvas_painter& target, const palette& colors, bool ascii, bool transparent,
                     string graph_symbol, bool rounded) :
    target_(target),
    palette_(colors),
    area_{0, 0, target.width(), target.height()},
    ascii_(ascii),
    transparent_(transparent),
    graph_symbol_(move(graph_symbol)),
    rounded_(rounded) {}

    painter painter::with_graph_symbol(string symbol) const {
        painter copy = *this;
        copy.graph_symbol_ = move(symbol);
        return copy;
    }

    painter painter::opaque() const {
        painter copy = *this;
        copy.transparent_ = false;
        return copy;
    }

    painter painter::clip(const rect& area) const {
        painter copy = *this;
        const int x = neforce::max(area.x_, area_.x_);
        const int y = neforce::max(area.y_, area_.y_);
        const int right = neforce::min(area.right(), area_.right());
        const int bottom = neforce::min(area.bottom(), area_.bottom());
        copy.area_ = rect{x, y, neforce::max(0, right - x), neforce::max(0, bottom - y)};
        return copy;
    }

    void painter::put(int x, int y, const codepoint& value, const tui::style& style) {
        if (!visible(x, y)) {
            return;
        }
        target_.put(x, y, value, style);
    }

    void painter::fill(const rect& area, string_view glyph, color fg, color bg) {
        const rect clipped = clip(area).area_;
        if (clipped.empty()) {
            return;
        }
        const auto base = [this](color value) {
            const bool background =
                    transparent_ && ((value.R() == palette_.bg_.R() && value.G() == palette_.bg_.G() &&
                                      value.B() == palette_.bg_.B()) ||
                                     (value.R() == palette_.panel_.R() && value.G() == palette_.panel_.G() &&
                                      value.B() == palette_.panel_.B()));
            return background ? color{0, 0, 0, 0} : move(value);
        };
        target_.fill(clipped.x_, clipped.y_, clipped.w_, clipped.h_, glyph, make_style(move(fg), base(move(bg))));
    }

    void painter::clear(const rect& area, color bg) { fill(area, " ", palette_.text_, move(bg)); }

    void painter::text(int x, int y, string_view content, color fg, optional<color> bg, bool bold) {
        if (y < area_.y_ || y >= area_.bottom()) {
            return;
        }
        if (transparent_ && bg.has_value() &&
            ((bg->R() == palette_.bg_.R() && bg->G() == palette_.bg_.G() && bg->B() == palette_.bg_.B()) ||
             (bg->R() == palette_.panel_.R() && bg->G() == palette_.panel_.G() && bg->B() == palette_.panel_.B()))) {
            bg = color{0, 0, 0, 0};
        }
        const tui::style value = make_style(move(fg), move(bg), bold);
        int column = x;
        size_t index = 0;
        while (index < content.size() && column < area_.right()) {
            const auto cp =
                    codepoint::decode_utf8(reinterpret_cast<const byte_t*>(content.data()), index, content.size());
            const int cell = cp.display_width();
            if (column >= area_.x_ && column + cell <= area_.right()) {
                put(column, y, cp, value);
            }
            column += cell;
        }
    }

    void painter::text_right(int x, int y, int width, string_view content, color fg, optional<color> bg, bool bold) {
        const int used = display_width(content);
        text(x + neforce::max(0, width - used), y, content, move(fg), move(bg), bold);
    }

    void painter::text_center(int x, int y, int width, string_view content, color fg) {
        const int used = display_width(content);
        text(x + neforce::max(0, (width - used) / 2), y, content, move(fg));
    }

    void painter::box(const rect& area, string_view title, color accent, optional<color> panel_color) {
        if (area.w_ < 2 || area.h_ < 2) {
            return;
        }
        const rect clipped = clip(area).area_;
        if (clipped.empty()) {
            return;
        }
        color background = panel_color.has_value() ? *panel_color : palette_.panel_;
        if (transparent_ && ((background.R() == palette_.bg_.R() && background.G() == palette_.bg_.G() &&
                              background.B() == palette_.bg_.B()) ||
                             (background.R() == palette_.panel_.R() && background.G() == palette_.panel_.G() &&
                              background.B() == palette_.panel_.B()))) {
            background = color{0, 0, 0, 0};
        }
        target_.fill(area.x_, area.y_, area.w_, area.h_, " ", make_style(palette_.text_, background));
        const tui::style border = make_style(palette_.border_, background);
        const bool ascii = ascii_;
        const bool rounded = rounded_ && !ascii;
        put(area.x_, area.y_, codepoint(ascii ? U'+' : (rounded ? U'╭' : U'┌')), border);
        put(area.right() - 1, area.y_, codepoint(ascii ? U'+' : (rounded ? U'╮' : U'┐')), border);
        put(area.x_, area.bottom() - 1, codepoint(ascii ? U'+' : (rounded ? U'╰' : U'└')), border);
        put(area.right() - 1, area.bottom() - 1, codepoint(ascii ? U'+' : (rounded ? U'╯' : U'┘')), border);
        hline(area.x_ + 1, area.y_, area.w_ - 2, ascii ? "-" : "─", palette_.border_, background);
        hline(area.x_ + 1, area.bottom() - 1, area.w_ - 2, ascii ? "-" : "─", palette_.border_, background);
        vline(area.x_, area.y_ + 1, area.h_ - 2, ascii ? "|" : "│", palette_.border_, background);
        vline(area.right() - 1, area.y_ + 1, area.h_ - 2, ascii ? "|" : "│", palette_.border_, background);
        if (!title.empty()) {
            const int maximum = neforce::max(0, area.w_ - 6);
            const string clipped_title = truncate_display(title, maximum);
            const int width = display_width(clipped_title.view());
            fill(rect{area.x_ + 2, area.y_, width + 2, 1}, " ", palette_.text_, background);
            text(area.x_ + 3, area.y_, clipped_title.view(), move(accent), background, true);
        }
    }

    void painter::hline(int x, int y, int width, string_view glyph, color fg, optional<color> bg) {
        if (y < area_.y_ || y >= area_.bottom()) {
            return;
        }
        const tui::style value = make_style(move(fg), move(bg));
        for (int index = 0; index < width; ++index) {
            const int column = x + index;
            if (column < area_.x_ || column >= area_.right()) {
                continue;
            }
            put(column, y, first_codepoint(glyph), value);
        }
    }

    void painter::vline(int x, int y, int height, string_view glyph, color fg, optional<color> bg) {
        if (x < area_.x_ || x >= area_.right()) {
            return;
        }
        const tui::style value = make_style(move(fg), move(bg));
        for (int index = 0; index < height; ++index) {
            const int row = y + index;
            if (row < area_.y_ || row >= area_.bottom()) {
                continue;
            }
            put(x, row, first_codepoint(glyph), value);
        }
    }

    void painter::track(int x, int y, int width, optional<double> value, double maximum, const color& accent) {
        if (y < area_.y_ || y >= area_.bottom() || width <= 0) {
            return;
        }
        const tui::style border_style = make_style(palette_.border_);
        if (!value.has_value() || !neforce::is_finite(*value) || !neforce::is_finite(maximum) || maximum <= 0.0) {
            for (int index = 0; index < width; ++index) {
                put(x + index, y, codepoint(ascii_ ? U'-' : U'┄'), border_style);
            }
            return;
        }
        const color filled = blend(palette_.panel_, accent);
        const tui::style fill_style = make_style(filled);
        const double units = neforce::clamp(*value / maximum, 0.0, 1.0) * static_cast<double>(width);
        const int whole = static_cast<int>(neforce::floor(units));
        const int fraction = static_cast<int>(neforce::floor((units - whole) * 8.0));
        static constexpr const char* partials[] = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
        for (int index = 0; index < whole && index < width; ++index) {
            put(x + index, y, codepoint(ascii_ ? U'#' : U'█'), fill_style);
        }
        int used = whole;
        if (whole < width && fraction > 0) {
            put(x + whole, y, ascii_ ? codepoint(U'#') : first_codepoint(partials[fraction]), fill_style);
            ++used;
        }
        for (int index = used; index < width; ++index) {
            put(x + index, y, codepoint(ascii_ ? U'-' : U'░'), border_style);
        }
    }

    namespace {

        constexpr string_view braille_up[25] = {
                " ", "⢀", "⢠", "⢰", "⢸", "⡀", "⣀", "⣠", "⣰", "⣸", "⡄", "⣄", "⣤",
                "⣴", "⣼", "⡆", "⣆", "⣦", "⣶", "⣾", "⡇", "⣇", "⣧", "⣷", "⣿",
        };

        constexpr string_view braille_down[25] = {
                " ", "⠈", "⠘", "⠸", "⢸", "⠁", "⠉", "⠙", "⠹", "⢹", "⠃", "⠋", "⠛",
                "⠻", "⢻", "⠇", "⠏", "⠟", "⠿", "⢿", "⡇", "⡏", "⡟", "⡿", "⣿",
        };

        constexpr string_view block_up[25] = {
                " ", "▗", "▗", "▐", "▐", "▖", "▄", "▄", "▟", "▟", "▖", "▄", "▄",
                "▟", "▟", "▌", "▙", "▙", "█", "█", "▌", "▙", "▙", "█", "█",
        };

        constexpr string_view block_down[25] = {
                " ", "▝", "▝", "▐", "▐", "▘", "▀", "▀", "▜", "▜", "▘", "▀", "▀",
                "▜", "▜", "▌", "▛", "▛", "█", "█", "▌", "▛", "▛", "█", "█",
        };

        constexpr string_view tty_up[25] = {
                " ", "░", "░", "▒", "▒", "░", "░", "▒", "▒", "█", "░", "▒", "▒",
                "▒", "█", "▒", "▒", "▒", "█", "█", "▒", "█", "█", "█", "█",
        };

        constexpr string_view tty_down[25] = {
                " ", "░", "░", "▒", "▒", "░", "░", "▒", "▒", "█", "░", "▒", "▒",
                "▒", "█", "▒", "▒", "▒", "█", "█", "▒", "█", "█", "█", "█",
        };

        string_view graph_glyph(const string& symbol, const int left, const int right, const bool mirrored) {
            const int index = neforce::clamp(left, 0, 4) * 5 + neforce::clamp(right, 0, 4);
            if (symbol == "block") {
                return mirrored ? block_down[index] : block_up[index];
            }
            if (symbol == "tty") {
                return mirrored ? tty_down[index] : tty_up[index];
            }
            return mirrored ? braille_down[index] : braille_up[index];
        }

    } // namespace

    void painter::raster(const rect& area, const vector<chart_series>& series, double maximum,
                         const plot_window& window, bool logarithmic, int direction) {
        if (area.empty()) {
            return;
        }
        const int width = area.w_ * 2;
        const int height = area.h_ * 4;
        vector<uint8_t> bits(static_cast<size_t>(area.w_ * area.h_), 0);
        vector<color> colors(static_cast<size_t>(area.w_ * area.h_), palette_.panel_);
        const bool mirrored = direction > 0;
        const bool cpu_mode = direction != 0;
        vector<color> row_colors;
        if (cpu_mode) {
            row_colors.reserve(static_cast<size_t>(area.h_));
            for (int row = 0; row < area.h_; ++row) {
                const int distance = mirrored ? row : area.h_ - 1 - row;
                row_colors.push_back(cpu_color(
                        static_cast<double>(distance) / static_cast<double>(neforce::max(1, area.h_ - 1)), palette_));
            }
        }
        const double max_value = neforce::max(1.0, maximum);
        const auto y_of = [&](double value) {
            value = neforce::clamp(value, 0.0, max_value);
            const double fraction =
                    logarithmic ? neforce::logarithm_1p(value) / neforce::logarithm_1p(max_value) : value / max_value;
            const double position = mirrored ? fraction : 1.0 - fraction;
            return static_cast<int>(neforce::round(position * (height - 1)));
        };
        const auto dot = [&](int x, int y, const color& tint) {
            if (x < 0 || y < 0 || x >= width || y >= height) {
                return;
            }
            const auto index = static_cast<size_t>(y / 4 * area.w_ + x / 2);
            if (index >= bits.size()) {
                return;
            }
            static constexpr int dots[2][4] = {{0, 1, 2, 6}, {3, 4, 5, 7}};
            bits[index] |= static_cast<uint8_t>(1U << dots[x % 2][y % 4]);
            colors[index] = cpu_mode ? row_colors[static_cast<size_t>(y / 4)] : tint;
        };
        vector<plot_range> columns;
        for (const auto& item: series) {
            if (item.values_ == nullptr) {
                continue;
            }
            window.project(*item.values_, static_cast<size_t>(width), columns);
            bool has_previous = false;
            int previous_x = 0;
            int previous_y = 0;
            for (size_t x = 0; x < columns.size(); ++x) {
                const auto& range = columns[x];
                if (!range.gap && range.high == 0.0 && range.low == 0.0 && range.last == 0.0) {
                    has_previous = false;
                    continue;
                }
                const int end = y_of(range.last);
                if (series.size() == 1) {
                    const int top = cpu_mode ? y_of(range.high) : end;
                    if (mirrored) {
                        for (int row = 0; row <= top; ++row) {
                            dot(static_cast<int>(x), row, item.color_);
                        }
                    } else {
                        for (int row = top; row < height; ++row) {
                            dot(static_cast<int>(x), row, item.color_);
                        }
                    }
                }
                const int high = y_of(range.high);
                const int low = y_of(range.low);
                for (int row = high; row <= low; ++row) {
                    dot(static_cast<int>(x), row, item.color_);
                }
                if (has_previous && !range.gap) {
                    line(previous_x, previous_y, static_cast<int>(x), end,
                         [&](int px, int py) { dot(px, py, item.color_); });
                }
                dot(static_cast<int>(x), end, item.color_);
                has_previous = !range.gap;
                previous_x = static_cast<int>(x);
                previous_y = end;
            }
        }
        for (int row = 0; row < area.h_; ++row) {
            for (int col = 0; col < area.w_; ++col) {
                const auto index = static_cast<size_t>(row * area.w_ + col);
                if (bits[index] == 0) {
                    continue;
                }
                int count = 0;
                for (uint8_t mask = 1; mask != 0; mask = static_cast<uint8_t>(mask << 1)) {
                    count += (bits[index] & mask) != 0 ? 1 : 0;
                }
                if (ascii_ || graph_symbol_ == "ascii") {
                    const char glyph = count >= 5 ? '#' : (count >= 3 ? '+' : (count >= 2 ? '-' : '.'));
                    put(area.x_ + col, area.y_ + row, codepoint(static_cast<uint32_t>(glyph)),
                        make_style(colors[index]));
                } else {
                    int left = 0;
                    int right = 0;
                    const uint8_t value = bits[index];
                    for (const int bit: {0, 1, 2, 6}) {
                        left += (value & static_cast<uint8_t>(1U << bit)) != 0 ? 1 : 0;
                    }
                    for (const int bit: {3, 4, 5, 7}) {
                        right += (value & static_cast<uint8_t>(1U << bit)) != 0 ? 1 : 0;
                    }
                    const string_view glyph = graph_glyph(graph_symbol_, left, right, mirrored);
                    put(area.x_ + col, area.y_ + row, first_codepoint(glyph), make_style(colors[index]));
                }
            }
        }
    }

    void painter::trend(const rect& area, const vector<chart_series>& series, double maximum, const plot_window& window,
                        string_view unit, bool axes, bool logarithmic) {
        rect body = area;
        if (axes && body.h_ >= 4 && body.w_ >= 14) {
            const string high = value_label(maximum, unit, display_config{});
            const string mid = value_label(maximum / 2.0, unit, display_config{});
            const string low = value_label(0.0, unit, display_config{});
            int left =
                    neforce::max({display_width(high.view()), display_width(mid.view()), display_width(low.view())}) +
                    1;
            if (left > body.w_ - 4) {
                left = 0;
            }
            if (left > 0) {
                text(body.x_, body.y_, high.view(), palette_.muted_);
                text(body.x_, body.y_ + (body.h_ - 2) / 2, mid.view(), palette_.muted_);
                text(body.x_, body.bottom() - 2, low.view(), palette_.muted_);
                text(body.x_ + left, body.bottom() - 1,
                     window.has_time
                             ? format("−{}", time_label(window.time.seconds)).view()
                             : format("{} samples", window.capacity(static_cast<size_t>(body.w_ - left) * 2)).view(),
                     palette_.muted_);
                text_right(body.x_ + left, body.bottom() - 1, body.w_ - left, "now", palette_.muted_);
                body.x_ += left;
                body.w_ -= left;
                body.h_ -= 1;
            }
        }
        raster(body, series, maximum, window, logarithmic, 0);
    }

    void painter::chart(const rect& area, string_view title, const vector<chart_series>& series, double maximum,
                        string_view unit, const plot_window& window, bool logarithmic) {
        if (area.h_ < 2) {
            return;
        }
        string caption = string(title);
        const string unit_text(unit.data(), unit.size());
        const string trimmed = trim(unit_text.view());
        if (!trimmed.empty() && !title.contains("(")) {
            caption = format("{} ({})", title, trimmed);
        }
        text(area.x_, area.y_, caption.view(), palette_.muted_);
        if (series.size() > 1) {
            int x = area.x_ + display_width(caption.view()) + 2;
            for (const auto& item: series) {
                const string legend = format("━━ {}", item.name_);
                text(x, area.y_, legend.view(), item.color_);
                x += display_width(legend.view()) + 1;
            }
        }
        trend(rect{area.x_, area.y_ + 1, area.w_, area.h_ - 1}, series, maximum, window, unit, true, logarithmic);
    }

    void painter::cpu_chart(const rect& area, const string& upper_name, const deque<double>* upper,
                            const string& lower_name, const deque<double>* lower, const plot_window& window) {
        if (area.h_ < 2 || area.w_ < 2) {
            return;
        }
        string title = "CPU history (%)";
        if (lower != nullptr && lower_name != upper_name) {
            title = format("CPU history (%) · ↑ {} / ↓ {}", upper_name, lower_name);
        } else if (upper_name != "total") {
            title = format("CPU history (%) · {}", upper_name);
        }
        text(area.x_, area.y_, title.view(), palette_.muted_);
        rect body{area.x_, area.y_ + 1, area.w_, area.h_ - 1};
        if (body.h_ >= 4 && body.w_ >= 14) {
            constexpr int axis = 5;
            text(body.x_ + axis, body.bottom() - 1,
                 window.has_time
                         ? format("−{}", time_label(window.time.seconds)).view()
                         : format("{} samples", window.capacity(static_cast<size_t>(body.w_ - axis) * 2)).view(),
                 palette_.muted_);
            text_right(body.x_ + axis, body.bottom() - 1, body.w_ - axis, "now", palette_.muted_);
            body.h_ -= 1;
            const int center = lower != nullptr ? (body.h_ + 1) / 2 - 1 : body.h_ - 1;
            text(body.x_, body.y_, "100%", palette_.muted_);
            text(body.x_, body.y_ + center, "0%", palette_.muted_);
            if (lower != nullptr) {
                text(body.x_, body.y_ + body.h_ - 1, "100%", palette_.muted_);
            }
            body.x_ += axis;
            body.w_ -= axis;
        }
        const int upper_height = lower != nullptr ? (body.h_ + 1) / 2 : body.h_;
        if (upper != nullptr) {
            raster(rect{body.x_, body.y_, body.w_, upper_height}, {chart_series{upper_name, upper, palette_.mint_}},
                   100.0, window, false, -1);
        } else {
            text(body.x_, body.y_, format("{} · unavailable", upper_name).view(), palette_.muted_);
        }
        if (lower != nullptr) {
            // The lower plot is mirrored: 0% is at the center line and 100% at the bottom.
            raster(rect{body.x_, body.y_ + upper_height, body.w_, body.h_ - upper_height},
                   {chart_series{lower_name, lower, palette_.mint_}}, 100.0, window, false, 1);
        }
    }

    void painter::empty(const rect& area, string_view title, string_view hint) {
        if (area.empty()) {
            return;
        }
        const int start = area.y_ + neforce::max(0, (area.h_ - 2) / 2);
        text_center(area.x_, start, area.w_, title, palette_.muted_);
        text_center(area.x_, start + 1, area.w_, hint, palette_.muted_);
    }

    void painter::scrollbar(const rect& area, size_t content, size_t viewport, size_t position) {
        if (area.empty() || viewport == 0 || content <= viewport) {
            return;
        }
        const int track = area.h_;
        const int thumb = neforce::max(1, static_cast<int>(static_cast<double>(viewport) / content * track));
        const int range = neforce::max(1, track - thumb);
        const int maximum = static_cast<int>(content - viewport);
        const int thumb_pos =
                maximum == 0
                        ? 0
                        : static_cast<int>(static_cast<double>(neforce::min(position, static_cast<size_t>(maximum))) /
                                           maximum * range);
        for (int row = 0; row < track; ++row) {
            const bool active = row >= thumb_pos && row < thumb_pos + thumb;
            put(area.x_, area.y_ + row, codepoint(active ? (ascii_ ? U'#' : U'█') : (ascii_ ? U'-' : U'░')),
                make_style(active ? palette_.mint_ : palette_.border_));
        }
    }

    string value_label(double value, string_view unit, const display_config& cfg) {
        if (!neforce::is_finite(value)) {
            return "—";
        }
        const string suffix(unit.data(), unit.size());
        if (suffix == "B") {
            return bytes(value);
        }
        if (suffix == "B/s") {
            return format("{}/s", bytes(value));
        }
        if (suffix == "bit/s" || suffix == "ibit/s" || suffix == "B/s SI") {
            const bool bits = suffix != "B/s SI";
            display_config adjusted = cfg;
            adjusted.network_bits_ = bits;
            adjusted.network_binary_ = suffix == "ibit/s";
            return adjusted.rate(bits ? value / 8.0 : value);
        }
        if (suffix == "%" || suffix == "°C" || suffix == "°F") {
            return format("{:.0f}{}", value, suffix);
        }
        if (suffix.empty()) {
            return format("{:.0f}", value);
        }
        return format("{:.0f} {}", value, suffix);
    }

    string time_label(uint64_t seconds) {
        if (seconds >= 3600) {
            return format("{}h", seconds / 3600);
        }
        if (seconds >= 60) {
            return format("{}m", seconds / 60);
        }
        return format("{}s", seconds);
    }

} // namespace neforce::neflux::ui
