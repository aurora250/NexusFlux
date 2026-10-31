#ifndef NEFLUX_CRASH_HPP__
#define NEFLUX_CRASH_HPP__
namespace neforce::neflux {

    /**
     * @brief 初始化进程级日志与崩溃捕获。
     *
     * 日志写入 `<config>/neflux/logs/nexus-flux.log`，启用大小轮转。
     * 崩溃捕获委托给 `crash_dump::install_handlers()`：
     * Windows 写出 `nexus-flux-<时间>-<pid>.dmp` minidump；
     * Linux x86-64/AArch64 写出 `nexus-flux-<时间>-<pid>.core` ELF core；
     * 崩溃摘要以 FATAL 级别写入同一目录，其他平台只记录致命信号摘要并退回内核 core 策略。
     * 初始化失败不会阻止程序启动。
     *
     * @param argc 参数个数
     * @param argv 参数数组
     */
    void initialize_crash_logging(int argc, char** argv);

} // namespace neforce::neflux
#endif // NEFLUX_CRASH_HPP__
