#include <NeFlux/common.hpp>
#include <NeFlux/model.hpp>
#include <NeFlux/ui/painter.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/tui/chart/canvas_chart.hpp>
namespace neforce::neflux::ui {

    namespace {

        struct island_raster_data {
            vector<deque<double>> values;
            vector<string> names;
            vector<color> colors;
            deque<double> timestamps;
            deque<double> periods;
            tui::canvas_chart_window window;
        };

        void paint_island_raster(painter& target, const rect& local, const shared_ptr<island_raster_data>& data,
                                 double maximum, bool logarithmic, int direction) {
            vector<chart_series> series;
            series.reserve(data->values.size());
            for (size_t index = 0; index < data->values.size(); ++index) {
                series.push_back({data->names[index], &data->values[index], data->colors[index]});
            }
            tui::canvas_chart_window window = data->window;
            if (window.time.timestamps != nullptr) {
                window.time.timestamps = &data->timestamps;
            }
            if (window.time.periods != nullptr) {
                window.time.periods = &data->periods;
            }
            target.raster(local, series, maximum, window, logarithmic, direction);
        }

        void paint_island_area(painter& target, const rect& local, const shared_ptr<island_raster_data>& data,
                               double maximum, bool logarithmic) {
            vector<chart_series> series;
            series.reserve(data->values.size());
            for (size_t index = 0; index < data->values.size(); ++index) {
                series.push_back({data->names[index], &data->values[index], data->colors[index]});
            }
            tui::canvas_chart_window window = data->window;
            if (window.time.timestamps != nullptr) {
                window.time.timestamps = &data->timestamps;
            }
            if (window.time.periods != nullptr) {
                window.time.periods = &data->periods;
            }
            target.area_raster(local, series, maximum, window, logarithmic);
        }

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

        codepoint first_codepoint(string_view text) {
            if (text.empty()) {
                return codepoint(U' ');
            }
            size_t index = 0;
            return codepoint::decode_utf8(reinterpret_cast<const byte_t*>(text.data()), index, text.size());
        }

        void apply_palette_defaults(palette& p) {
            p.cpu_start_ = p.mint_;
            p.cpu_mid_ = p.amber_;
            p.cpu_end_ = p.red_;
            p.mem_start_ = p.blue_;
            p.mem_mid_ = p.amber_;
            p.mem_end_ = p.red_;
            p.net_start_ = p.mint_;
            p.net_mid_ = p.blue_;
            p.net_end_ = p.purple_;
            p.gpu_start_ = p.blue_;
            p.gpu_mid_ = p.purple_;
            p.gpu_end_ = p.red_;
            p.temp_start_ = p.mint_;
            p.temp_mid_ = p.amber_;
            p.temp_end_ = p.red_;
            p.proc_start_ = p.mint_;
            p.proc_mid_ = p.amber_;
            p.proc_end_ = p.red_;
            p.meter_bg_ = blend(p.bg_, p.border_);
            p.graph_text_ = p.muted_;
            p.followed_bg_ = blend(p.panel_, p.blue_);
            p.followed_fg_ = p.text_;
            p.pause_bg_ = blend(p.panel_, p.amber_);
            p.follow_bg_ = blend(p.panel_, p.mint_);
            p.banner_bg_ = blend(p.panel_, p.purple_);
            p.banner_fg_ = p.text_;
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
        apply_palette_defaults(p);
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
            } else if (member.first == "cpu_start") {
                p.cpu_start_ = value;
            } else if (member.first == "cpu_mid") {
                p.cpu_mid_ = value;
            } else if (member.first == "cpu_end") {
                p.cpu_end_ = value;
            } else if (member.first == "mem_start") {
                p.mem_start_ = value;
            } else if (member.first == "mem_mid") {
                p.mem_mid_ = value;
            } else if (member.first == "mem_end") {
                p.mem_end_ = value;
            } else if (member.first == "net_start") {
                p.net_start_ = value;
            } else if (member.first == "net_mid") {
                p.net_mid_ = value;
            } else if (member.first == "net_end") {
                p.net_end_ = value;
            } else if (member.first == "gpu_start") {
                p.gpu_start_ = value;
            } else if (member.first == "gpu_mid") {
                p.gpu_mid_ = value;
            } else if (member.first == "gpu_end") {
                p.gpu_end_ = value;
            } else if (member.first == "temp_start") {
                p.temp_start_ = value;
            } else if (member.first == "temp_mid") {
                p.temp_mid_ = value;
            } else if (member.first == "temp_end") {
                p.temp_end_ = value;
            } else if (member.first == "proc_start") {
                p.proc_start_ = value;
            } else if (member.first == "proc_mid") {
                p.proc_mid_ = value;
            } else if (member.first == "proc_end") {
                p.proc_end_ = value;
            } else if (member.first == "meter_bg") {
                p.meter_bg_ = value;
            } else if (member.first == "graph_text") {
                p.graph_text_ = value;
            } else if (member.first == "followed_bg") {
                p.followed_bg_ = value;
            } else if (member.first == "followed_fg") {
                p.followed_fg_ = value;
            } else if (member.first == "pause_bg" || member.first == "proc_pause_bg") {
                p.pause_bg_ = value;
            } else if (member.first == "follow_bg" || member.first == "proc_follow_bg") {
                p.follow_bg_ = value;
            } else if (member.first == "banner_bg" || member.first == "proc_banner_bg") {
                p.banner_bg_ = value;
            } else if (member.first == "banner_fg" || member.first == "proc_banner_fg") {
                p.banner_fg_ = value;
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
        for (color* value:
             {&colors.cpu_start_,   &colors.cpu_mid_,     &colors.cpu_end_,    &colors.mem_start_, &colors.mem_mid_,
              &colors.mem_end_,     &colors.net_start_,   &colors.net_mid_,    &colors.net_end_,   &colors.gpu_start_,
              &colors.gpu_mid_,     &colors.gpu_end_,     &colors.temp_start_, &colors.temp_mid_,  &colors.temp_end_,
              &colors.proc_start_,  &colors.proc_mid_,    &colors.proc_end_,   &colors.meter_bg_,  &colors.graph_text_,
              &colors.followed_bg_, &colors.followed_fg_, &colors.pause_bg_,   &colors.follow_bg_, &colors.banner_bg_,
              &colors.banner_fg_}) {
            *value = gray(*value);
        }
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

    void cell_grid::reset(const rect& area) {
        area_ = area;
        islands_.clear();
        const size_t columns = static_cast<size_t>(neforce::max(0, area.w_));
        const size_t rows = static_cast<size_t>(neforce::max(0, area.h_));
        cells_.assign(columns * rows, grid_cell{});
    }

    void cell_grid::put(const int x, const int y, const codepoint& value, const tui::style& style) {
        const int column = x - area_.x_;
        const int row = y - area_.y_;
        if (column < 0 || row < 0 || column >= area_.w_ || row >= area_.h_) {
            return;
        }
        const size_t index = static_cast<size_t>(row) * static_cast<size_t>(area_.w_) + static_cast<size_t>(column);
        auto& cell = cells_[index];
        cell.value = value;
        cell.continuation = false;
        cell.painted = true;
        cell.fg = style.fg.has_value() ? *style.fg : color{0, 0, 0, 0};
        cell.bg = style.bg.has_value() ? *style.bg : color{0, 0, 0, 0};
        cell.bold = style.bold.value_or(false);
        const int width = value.display_width();
        for (int extra = 1; extra < width; ++extra) {
            if (column + extra >= area_.w_) {
                break;
            }
            auto& next = cells_[index + static_cast<size_t>(extra)];
            next.value = codepoint(U' ');
            next.continuation = true;
            next.painted = true;
            next.bold = false;
        }
    }

    void cell_grid::fill(const int x, const int y, const int width, const int height, string_view glyph,
                         const tui::style& style) {
        const codepoint value = first_codepoint(glyph);
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                put(x + column, y + row, value, style);
            }
        }
    }

    const grid_cell& cell_grid::at(const int column, const int row) const noexcept {
        static const grid_cell blank{};
        if (column < 0 || row < 0 || column >= area_.w_ || row >= area_.h_) {
            return blank;
        }
        return cells_[static_cast<size_t>(row) * static_cast<size_t>(area_.w_) + static_cast<size_t>(column)];
    }

    void cell_grid::add_island(plot_island island) { islands_.push_back(move(island)); }

    painter::painter(tui::canvas_painter& target, const palette& colors, bool ascii, bool transparent,
                     string graph_symbol, bool rounded, bool graph_gradient) :
    canvas_(&target),
    palette_(colors),
    area_{0, 0, target.width(), target.height()},
    ascii_(ascii),
    transparent_(transparent),
    graph_symbol_(move(graph_symbol)),
    rounded_(rounded),
    graph_gradient_(graph_gradient),
    panes_(make_shared<vector<pane>>()) {}

    painter::painter(cell_grid& target, const palette& colors, bool ascii, bool transparent, string graph_symbol,
                     bool rounded, bool graph_gradient) :
    grid_(&target),
    palette_(colors),
    area_{target.area().x_, target.area().y_, target.width(), target.height()},
    ascii_(ascii),
    transparent_(transparent),
    graph_symbol_(move(graph_symbol)),
    rounded_(rounded),
    graph_gradient_(graph_gradient),
    panes_(make_shared<vector<pane>>()) {}

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
        if (grid_ != nullptr) {
            grid_->put(x, y, value, style);
            return;
        }
        canvas_->put(x, y, value, style);
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
        if (grid_ != nullptr) {
            grid_->fill(clipped.x_, clipped.y_, clipped.w_, clipped.h_, glyph, make_style(move(fg), base(move(bg))));
            return;
        }
        canvas_->fill(clipped.x_, clipped.y_, clipped.w_, clipped.h_, glyph, make_style(move(fg), base(move(bg))));
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
        const tui::style background_style = make_style(palette_.text_, background);
        if (grid_ != nullptr) {
            grid_->fill(area.x_, area.y_, area.w_, area.h_, " ", background_style);
        } else {
            canvas_->fill(area.x_, area.y_, area.w_, area.h_, " ", background_style);
        }
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
        const string clipped_title = title.empty() ? string{} : truncate_display(title, neforce::max(0, area.w_ - 6));
        if (grid_ != nullptr) {
            pane entry;
            entry.area_ = area;
            entry.title_ = clipped_title;
            entry.accent_ = accent;
            entry.background_ = background;
            entry.ascii_ = ascii_;
            entry.rounded_ = rounded_ && !ascii_;
            panes_->push_back(move(entry));
        }
        if (!clipped_title.empty()) {
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
        if (grid_ != nullptr) {
            tui::style empty_style;
            empty_style.fg = palette_.meter_bg_;
            tui::style fill_style;
            fill_style.fg = blend(palette_.panel_, accent);
            if (!value.has_value() || !neforce::is_finite(*value) || !neforce::is_finite(maximum) || maximum <= 0.0) {
                for (int index = 0; index < width; ++index) {
                    put(x + index, y, codepoint(ascii_ ? U'-' : U'┄'), empty_style);
                }
                return;
            }
            const double units = neforce::clamp(*value / maximum, 0.0, 1.0) * static_cast<double>(width);
            const int whole = static_cast<int>(neforce::floor(units));
            const int fraction = static_cast<int>(neforce::floor((units - whole) * 8.0));
            static constexpr const char* partials[] = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
            for (int index = 0; index < whole && index < width; ++index) {
                put(x + index, y, codepoint(ascii_ ? U'#' : U'█'), fill_style);
            }
            int used = whole;
            if (whole < width && fraction > 0) {
                put(x + whole, y, first_codepoint(ascii_ ? string_view("#") : string_view(partials[fraction])),
                    fill_style);
                ++used;
            }
            for (int index = used; index < width; ++index) {
                put(x + index, y, codepoint(ascii_ ? U'-' : U'░'), empty_style);
            }
            return;
        }
        tui::canvas_track(*canvas_, x, y, width, value, maximum, blend(palette_.panel_, accent), palette_.meter_bg_,
                          ascii_);
    }


    void painter::raster(const rect& area, const vector<chart_series>& series, double maximum,
                         const plot_window& window, bool logarithmic, int direction) {
        if (area.empty()) {
            return;
        }
        if (grid_ != nullptr) {
            auto data = make_shared<island_raster_data>();
            data->window = window;
            if (window.time.timestamps != nullptr) {
                data->timestamps = *window.time.timestamps;
            }
            if (window.time.periods != nullptr) {
                data->periods = *window.time.periods;
            }
            data->values.resize(series.size());
            data->names.reserve(series.size());
            data->colors.reserve(series.size());
            for (size_t index = 0; index < series.size(); ++index) {
                data->names.push_back(series[index].name_);
                data->colors.push_back(series[index].color_);
                if (series[index].values_ != nullptr) {
                    data->values[index] = *series[index].values_;
                }
            }
            grid_->add_island({area, [data, maximum, logarithmic, direction](painter& target, const rect& local) {
                                   paint_island_raster(target, local, data, maximum, logarithmic, direction);
                               }});
            return;
        }
        tui::canvas_chart_style style;
        style.panel = palette_.panel_;
        style.graph_text = palette_.graph_text_;
        style.cpu_start = palette_.cpu_start_;
        style.cpu_mid = palette_.cpu_mid_;
        style.cpu_end = palette_.cpu_end_;
        style.graph_symbol = graph_symbol_;
        style.ascii = ascii_;
        style.gradient = graph_gradient_;
        vector<tui::chart_series> converted;
        converted.reserve(series.size());
        for (const auto& item: series) {
            tui::chart_series value;
            value.name = item.name_;
            value.stream = item.values_;
            value.color = item.color_;
            converted.push_back(move(value));
        }
        tui::canvas_chart(*canvas_, move(style))
                .raster(area.x_, area.y_, area.w_, area.h_, converted, maximum, window, logarithmic, direction);
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
                text(body.x_, body.y_, high.view(), palette_.graph_text_);
                text(body.x_, body.y_ + (body.h_ - 2) / 2, mid.view(), palette_.graph_text_);
                text(body.x_, body.bottom() - 2, low.view(), palette_.graph_text_);
                text(body.x_ + left, body.bottom() - 1,
                     window.has_time
                             ? format("−{}", time_label(window.time.seconds)).view()
                             : format("{} samples", window.capacity(static_cast<size_t>(body.w_ - left) * 2)).view(),
                     palette_.graph_text_);
                text_right(body.x_ + left, body.bottom() - 1, body.w_ - left, "now", palette_.graph_text_);
                body.x_ += left;
                body.w_ -= left;
                body.h_ -= 1;
            }
        }
        raster(body, series, maximum, window, logarithmic, 0);
    }

    void painter::area(const rect& area, const vector<chart_series>& series, double maximum, const plot_window& window,
                       bool logarithmic) {
        if (area.empty() || series.empty()) {
            return;
        }
        if (grid_ != nullptr) {
            auto data = make_shared<island_raster_data>();
            data->window = window;
            if (window.time.timestamps != nullptr) {
                data->timestamps = *window.time.timestamps;
            }
            if (window.time.periods != nullptr) {
                data->periods = *window.time.periods;
            }
            data->values.resize(series.size());
            data->names.reserve(series.size());
            data->colors.reserve(series.size());
            for (size_t index = 0; index < series.size(); ++index) {
                data->names.push_back(series[index].name_);
                data->colors.push_back(series[index].color_);
                if (series[index].values_ != nullptr) {
                    data->values[index] = *series[index].values_;
                }
            }
            grid_->add_island({area, [data, maximum, logarithmic](painter& target, const rect& local) {
                                   paint_island_area(target, local, data, maximum, logarithmic);
                               }});
            return;
        }
        area_raster(area, series, maximum, window, logarithmic);
    }

    void painter::area_raster(const rect& area, const vector<chart_series>& series, double maximum,
                              const plot_window& window, bool logarithmic) {
        if (canvas_ == nullptr || area.empty() || series.empty()) {
            return;
        }
        tui::canvas_chart_style style;
        style.panel = palette_.panel_;
        style.graph_text = palette_.graph_text_;
        const color tint = series.front().color_;
        const auto mix = [](int from, int to, int weight) { return (from * (10 - weight) + to * weight) / 10; };
        style.cpu_start = color{mix(palette_.panel_.R(), tint.R(), 4), mix(palette_.panel_.G(), tint.G(), 4),
                                mix(palette_.panel_.B(), tint.B(), 4)};
        style.cpu_mid = color{mix(palette_.panel_.R(), tint.R(), 7), mix(palette_.panel_.G(), tint.G(), 7),
                              mix(palette_.panel_.B(), tint.B(), 7)};
        style.cpu_end = tint;
        style.graph_symbol = graph_symbol_;
        style.ascii = ascii_;
        style.gradient = true;
        vector<tui::chart_series> converted;
        converted.reserve(series.size());
        for (const auto& item: series) {
            tui::chart_series value;
            value.name = item.name_;
            value.stream = item.values_;
            value.color = item.color_;
            converted.push_back(move(value));
        }
        tui::canvas_chart(*canvas_, move(style))
                .raster(area.x_, area.y_, area.w_, area.h_, converted, maximum, window, logarithmic, -1);
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
        text(area.x_, area.y_, caption.view(), palette_.graph_text_);
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
        text(area.x_, area.y_, title.view(), palette_.graph_text_);
        rect body{area.x_, area.y_ + 1, area.w_, area.h_ - 1};
        if (body.h_ >= 4 && body.w_ >= 14) {
            constexpr int axis = 5;
            text(body.x_ + axis, body.bottom() - 1,
                 window.has_time
                         ? format("−{}", time_label(window.time.seconds)).view()
                         : format("{} samples", window.capacity(static_cast<size_t>(body.w_ - axis) * 2)).view(),
                 palette_.graph_text_);
            text_right(body.x_ + axis, body.bottom() - 1, body.w_ - axis, "now", palette_.graph_text_);
            body.h_ -= 1;
            const int center = lower != nullptr ? (body.h_ + 1) / 2 - 1 : body.h_ - 1;
            text(body.x_, body.y_, "100%", palette_.graph_text_);
            text(body.x_, body.y_ + center, "0%", palette_.graph_text_);
            if (lower != nullptr) {
                text(body.x_, body.y_ + body.h_ - 1, "100%", palette_.graph_text_);
            }
            body.x_ += axis;
            body.w_ -= axis;
        }
        const int upper_height = lower != nullptr ? (body.h_ + 1) / 2 : body.h_;
        if (upper != nullptr) {
            raster(rect{body.x_, body.y_, body.w_, upper_height}, {chart_series{upper_name, upper, palette_.mint_}},
                   100.0, window, false, -1);
        } else {
            text(body.x_, body.y_, format("{} · unavailable", upper_name).view(), palette_.graph_text_);
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
        if (area.empty()) {
            return;
        }
        if (grid_ != nullptr) {
            if (viewport == 0 || content <= viewport) {
                return;
            }
            const int height = area.h_;
            const int thumb_size = neforce::max(1, static_cast<int>(static_cast<double>(viewport) / content * height));
            const int range = neforce::max(1, height - thumb_size);
            const int maximum = static_cast<int>(content - viewport);
            const int thumb_pos =
                    maximum == 0 ? 0
                                 : static_cast<int>(
                                           static_cast<double>(neforce::min(position, static_cast<size_t>(maximum))) /
                                           maximum * range);
            tui::style thumb_style;
            thumb_style.fg = palette_.mint_;
            tui::style track_style;
            track_style.fg = palette_.border_;
            for (int row = 0; row < height; ++row) {
                const bool active = row >= thumb_pos && row < thumb_pos + thumb_size;
                put(area.x_, area.y_ + row, codepoint(active ? (ascii_ ? U'#' : U'█') : (ascii_ ? U'-' : U'░')),
                    active ? thumb_style : track_style);
            }
            return;
        }
        tui::canvas_scrollbar(*canvas_, area.x_, area.y_, area.h_, content, viewport, position, palette_.mint_,
                              palette_.border_, ascii_);
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
