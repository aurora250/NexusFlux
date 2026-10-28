#ifndef NEXUSFLUX_UI_PAINTER_HPP__
#define NEXUSFLUX_UI_PAINTER_HPP__

/**
 * @file painter.hpp
 * @brief 基于 NexusForce canvas 的终端绘制助手
 *
 * 提供面板、文本、仪表与盲文图表，坐标均以终端单元格计。
 */

#include <NeFlux/config.hpp>
#include <NeFlux/plot.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/string/codepoint.hpp>
#include <NeForce/tui/dom/canvas.hpp>
#include <NeForce/tui/dom/style.hpp>
namespace neforce::neflux::ui {

    /**
     * @brief 终端矩形
     */
    struct rect {
        int x_ = 0; ///< 列
        int y_ = 0; ///< 行
        int w_ = 0; ///< 宽
        int h_ = 0; ///< 高

        /// @brief 右边界
        NEFORCE_NODISCARD int right() const noexcept { return x_ + w_; }
        /// @brief 下边界
        NEFORCE_NODISCARD int bottom() const noexcept { return y_ + h_; }
        /// @brief 是否为空
        NEFORCE_NODISCARD bool empty() const noexcept { return w_ <= 0 || h_ <= 0; }
        /// @brief 内缩
        NEFORCE_NODISCARD rect inset(int amount) const noexcept {
            return rect{x_ + amount, y_ + amount, w_ - amount * 2, h_ - amount * 2};
        }
        /// @brief 是否包含坐标
        NEFORCE_NODISCARD bool contains(int x, int y) const noexcept {
            return x >= x_ && y >= y_ && x < right() && y < bottom();
        }
    };

    /**
     * @brief 界面配色
     */
    struct palette {
        color bg_;        ///< 背景
        color panel_;     ///< 面板底色
        color border_;    ///< 边框
        color muted_;     ///< 弱化文本
        color text_;      ///< 正文
        color mint_;      ///< 主强调色
        color blue_;      ///< 次强调色
        color purple_;    ///< 第三强调色
        color amber_;     ///< 警告
        color red_;       ///< 危险
        color selection_; ///< 选中行底色

        color cpu_start_;   ///< CPU 渐变起点
        color cpu_mid_;     ///< CPU 渐变中点
        color cpu_end_;     ///< CPU 渐变终点
        color mem_start_;   ///< 内存渐变起点
        color mem_mid_;     ///< 内存渐变中点
        color mem_end_;     ///< 内存渐变终点
        color net_start_;   ///< 网络渐变起点
        color net_mid_;     ///< 网络渐变中点
        color net_end_;     ///< 网络渐变终点
        color gpu_start_;   ///< GPU 渐变起点
        color gpu_mid_;     ///< GPU 渐变中点
        color gpu_end_;     ///< GPU 渐变终点
        color temp_start_;  ///< 温度渐变起点
        color temp_mid_;    ///< 温度渐变中点
        color temp_end_;    ///< 温度渐变终点
        color proc_start_;  ///< 进程渐变起点
        color proc_mid_;    ///< 进程渐变中点
        color proc_end_;    ///< 进程渐变终点
        color meter_bg_;    ///< 仪表背景
        color graph_text_;  ///< 图表文字
        color followed_bg_; ///< 跟随进程底色
        color followed_fg_; ///< 跟随进程文字
        color pause_bg_;    ///< 暂停横幅底色
        color follow_bg_;   ///< 跟随横幅底色
        color banner_bg_;   ///< 暂停+跟随横幅底色
        color banner_fg_;   ///< 横幅文字
    };

    /**
     * @brief 获取主题配色
     * @param name 主题名
     * @param custom 自定义颜色覆盖
     * @return 配色
     */
    NEFORCE_NODISCARD palette make_palette(const string& name, const json& custom);

    /**
     * @brief 将配色转换为单色
     * @param source 原配色
     * @return 单色配色
     */
    NEFORCE_NODISCARD palette monochrome_palette(const palette& source);

    /**
     * @brief 获取状态文本颜色
     * @param value 状态
     * @param p 配色
     * @return 颜色
     */
    NEFORCE_NODISCARD color state_color(const string& value, const palette& p);

    /**
     * @brief 图表序列
     */
    struct chart_series {
        string name_;                 ///< 名称
        const deque<double>* values_; ///< 数值
        color color_;                 ///< 颜色
    };

    /// @cond
    class painter;
    /// @endcond

    /**
     * @brief 内容区单元格
     */
    struct grid_cell {
        codepoint value{U' '};     ///< 字符
        color fg{0, 0, 0, 0};      ///< 前景色，透明表示沿用终端默认前景
        color bg{0, 0, 0, 0};      ///< 背景色，透明表示沿用终端默认背景
        bool bold = false;         ///< 是否加粗
        bool continuation = false; ///< 是否宽字符占据的续列
        bool painted = false;      ///< 是否被绘制过
    };

    /**
     * @brief 图表画布岛
     *
     * 内容区为 DOM 行，图表/仪表/滚动条仍由 canvas 渲染：
     * 记录屏幕矩形与局部坐标重绘回调，由 DOM 层叠加一个 canvas 元素。
     */
    struct plot_island {
        rect area;                                   ///< 屏幕坐标矩形
        function<void(painter&, const rect&)> paint; ///< 以局部坐标重绘
    };

    /**
     * @brief 内容区单元格缓冲
     */
    class cell_grid {
    public:
        /**
         * @brief 重置为新的屏幕区域
         * @param area 屏幕坐标矩形
         */
        void reset(const rect& area);

        /// @brief 宽度
        NEFORCE_NODISCARD int width() const noexcept { return area_.w_; }
        /// @brief 高度
        NEFORCE_NODISCARD int height() const noexcept { return area_.h_; }
        /// @brief 屏幕区域
        NEFORCE_NODISCARD const rect& area() const noexcept { return area_; }

        /**
         * @brief 写入单元格（屏幕坐标）
         * @param x 列
         * @param y 行
         * @param value 字符
         * @param style 样式
         */
        void put(int x, int y, const codepoint& value, const tui::style& style);

        /**
         * @brief 填充矩形（屏幕坐标）
         * @param x 列
         * @param y 行
         * @param width 宽
         * @param height 高
         * @param glyph 字符
         * @param style 样式
         */
        void fill(int x, int y, int width, int height, string_view glyph, const tui::style& style);

        /**
         * @brief 读取单元格
         * @param column 局部列
         * @param row 局部行
         * @return 单元格
         */
        NEFORCE_NODISCARD const grid_cell& at(int column, int row) const noexcept;

        /**
         * @brief 登记图表画布岛
         * @param island 画布岛
         */
        void add_island(plot_island island);

        /// @brief 图表画布岛列表
        NEFORCE_NODISCARD const vector<plot_island>& islands() const noexcept { return islands_; }

    private:
        rect area_{};
        vector<grid_cell> cells_;
        vector<plot_island> islands_;
    };

    /**
     * @brief 绘制助手
     */
    class painter {
    public:
        /**
         * @brief 构造绘制助手
         * @param target 画布目标
         * @param colors 配色
         * @param ascii 是否使用纯 ASCII 符号
         * @param transparent 是否使用终端默认背景
         * @param graph_symbol
         * @param rounded
         */
        painter(tui::canvas_painter& target, const palette& colors, bool ascii = false, bool transparent = false,
                string graph_symbol = "braille", bool rounded = true, bool graph_gradient = true);

        /**
         * @brief 构造内容区单元格绘制助手
         * @param target 单元格缓冲，坐标为屏幕坐标
         * @param colors 配色
         * @param ascii 是否使用纯 ASCII 符号
         * @param transparent 是否使用终端默认背景
         * @param graph_symbol
         * @param rounded
         * @param graph_gradient
         * @note 图表类调用会登记画布岛，由 DOM 层叠加 canvas 渲染
         */
        painter(cell_grid& target, const palette& colors, bool ascii = false, bool transparent = false,
                string graph_symbol = "braille", bool rounded = true, bool graph_gradient = true);

        /**
         * @brief 创建裁剪区域子绘制器
         * @param area 新裁剪区
         * @return 子绘制器
         */
        NEFORCE_NODISCARD painter clip(const rect& area) const;

        /**
         * @brief 创建不透明子绘制器
         * @return 关闭透明背景的绘制器
         * @note 用于弹窗和浮层，避免终端透明背景透出后方动态内容
         */
        NEFORCE_NODISCARD painter opaque() const;

        /**
         * @brief 创建替换图表符号的子绘制器
         * @param symbol 图表符号
         * @return 子绘制器
         */
        NEFORCE_NODISCARD painter with_graph_symbol(string symbol) const;

        /// @brief 可用宽度
        NEFORCE_NODISCARD int width() const noexcept { return area_.w_; }
        /// @brief 可用高度
        NEFORCE_NODISCARD int height() const noexcept { return area_.h_; }
        /// @brief 当前裁剪区
        NEFORCE_NODISCARD const rect& area() const noexcept { return area_; }
        /// @brief 配色
        NEFORCE_NODISCARD const palette& colors() const noexcept { return palette_; }

        /**
         * @brief 面板记录
         *
         * `box()` 在绘制的同时登记面板几何与样式，DOM 内容层据此生成
         * `tui::element::panel`，由组件负责边框、标题与底色。
         */
        struct pane {
            rect area_;           ///< 面板屏幕矩形
            string title_;        ///< 已截断的标题
            color accent_;        ///< 标题色
            color background_;    ///< 面板底色（透明时为 alpha 0）
            bool ascii_ = false;  ///< ASCII 边框
            bool rounded_ = true; ///< 圆角边框
        };

        /// @brief 本次绘制登记的面板
        NEFORCE_NODISCARD const vector<pane>& panes() const noexcept { return *panes_; }

        /**
         * @brief 填充矩形
         * @param area 区域
         * @param glyph 字符
         * @param fg 前景
         * @param bg 背景
         */
        void fill(const rect& area, string_view glyph, color fg, color bg);

        /**
         * @brief 使用默认背景填充
         * @param area 区域
         * @param bg 背景
         */
        void clear(const rect& area, color bg);

        /**
         * @brief 绘制文本
         * @param x 列
         * @param y 行
         * @param content 文本
         * @param fg 前景
         * @param bg 背景
         * @param bold 是否加粗
         */
        void text(int x, int y, string_view content, color fg, optional<color> bg = none, bool bold = false);

        /**
         * @brief 右对齐文本
         * @param x 起始列
         * @param y 行
         * @param width 区域宽度
         * @param content 文本
         * @param fg 前景
         * @param bg 背景
         * @param bold 是否加粗
         */
        void text_right(int x, int y, int width, string_view content, color fg, optional<color> bg = none,
                        bool bold = false);

        /**
         * @brief 居中文本
         * @param x 起始列
         * @param y 行
         * @param width 区域宽度
         * @param content 文本
         * @param fg 前景
         */
        void text_center(int x, int y, int width, string_view content, color fg);

        /**
         * @brief 绘制带标题的圆角面板
         * @param area 区域
         * @param title 标题
         * @param accent 标题颜色
         * @param panel_color 面板底色
         */
        void box(const rect& area, string_view title, color accent, optional<color> panel_color = none);

        /**
         * @brief 绘制水平线
         * @param x 列
         * @param y 行
         * @param width 长度
         * @param glyph 字符
         * @param fg 前景
         */
        void hline(int x, int y, int width, string_view glyph, color fg, optional<color> bg = none);

        /**
         * @brief 绘制垂直线
         * @param x 列
         * @param y 行
         * @param height 长度
         * @param glyph 字符
         * @param fg 前景
         */
        void vline(int x, int y, int height, string_view glyph, color fg, optional<color> bg = none);

        /**
         * @brief 绘制固定刻度仪表
         * @param x 列
         * @param y 行
         * @param width 宽度
         * @param value 当前值
         * @param maximum 上限
         * @param accent 强调色
         */
        void track(int x, int y, int width, optional<double> value, double maximum, const color& accent);

        /**
         * @brief 绘制趋势图
         * @param area 区域
         * @param series 序列
         * @param maximum 上限
         * @param window 窗口
         * @param unit 单位
         * @param axes 是否绘制坐标轴
         * @param logarithmic 是否对数刻度
         */
        void trend(const rect& area, const vector<chart_series>& series, double maximum, const plot_window& window,
                   string_view unit, bool axes, bool logarithmic = false);

        /**
         * @brief 绘制带渐变填充的面积图（用于 MEMORY 五项 sparkline）
         */
        void area(const rect& area, const vector<chart_series>& series, double maximum, const plot_window& window,
                  bool logarithmic = false);

        /**
         * @brief 在 canvas 上直接绘制面积图（画布岛回调使用）
         */
        void area_raster(const rect& area, const vector<chart_series>& series, double maximum,
                         const plot_window& window, bool logarithmic);

        /**
         * @brief 绘制带标题的趋势图
         * @param area 区域
         * @param title 标题
         * @param series 序列
         * @param maximum 上限
         * @param unit 单位
             * @param window 窗口
         * @param logarithmic 是否对数刻度
         */
        void chart(const rect& area, string_view title, const vector<chart_series>& series, double maximum,
                   string_view unit, const plot_window& window, bool logarithmic = false);

        /**
         * @brief 绘制 CPU 镜像图表
         * @param area 区域
         * @param upper_name 上半指标名
         * @param upper 上半序列
         * @param lower_name 下半指标名
         * @param lower 下半序列
         * @param window 窗口
         */
        void cpu_chart(const rect& area, const string& upper_name, const deque<double>* upper, const string& lower_name,
                       const deque<double>* lower, const plot_window& window);

        /**
         * @brief 绘制空状态
         * @param area 区域
         * @param title 标题
         * @param hint 提示
         */
        void empty(const rect& area, string_view title, string_view hint);

        /**
         * @brief 绘制滚动条
         * @param area 区域
         * @param content 内容行数
         * @param viewport 可视行数
         * @param position 顶部位置
         */
        void scrollbar(const rect& area, size_t content, size_t viewport, size_t position);

        /**
         * @brief 只绘制图表光栅（不含标题与坐标轴）
         * @param area 区域
         * @param series 序列
         * @param maximum 上限
         * @param window 窗口
         * @param logarithmic 是否对数刻度
         * @param direction 绘制方向，-1/0/1
         * @note 供 DOM 内容层的画布岛在渲染阶段重绘使用
         */
        void raster(const rect& area, const vector<chart_series>& series, double maximum, const plot_window& window,
                    bool logarithmic, int direction);

        /**
         * @brief 获取底层画布
         * @return 画布引用
         * @note 仅在画布模式下有效
         */
        NEFORCE_NODISCARD tui::canvas_painter& canvas() const noexcept { return *canvas_; }

    private:
        tui::canvas_painter* canvas_ = nullptr;
        cell_grid* grid_ = nullptr;
        const palette& palette_;
        rect area_;
        bool ascii_ = false;
        bool transparent_ = false;
        string graph_symbol_ = "braille";
        bool rounded_ = true;
        bool graph_gradient_ = true;
        shared_ptr<vector<pane>> panes_;

        NEFORCE_NODISCARD bool visible(int x, int y) const noexcept {
            return x >= area_.x_ && y >= area_.y_ && x < area_.right() && y < area_.bottom();
        }
        void put(int x, int y, const codepoint& value, const tui::style& style);
    };

    /**
     * @brief 格式化带单位的图表数值
     * @param value 数值
     * @param unit 单位
     * @param cfg 配置
     * @return 文本
     */
    NEFORCE_NODISCARD string value_label(double value, string_view unit, const display_config& cfg);

    /**
     * @brief 获取时间窗口标签
     * @param seconds 秒
     * @return 文本
     */
    NEFORCE_NODISCARD string time_label(uint64_t seconds);

} // namespace neforce::neflux::ui
#endif // NEXUSFLUX_UI_PAINTER_HPP__
