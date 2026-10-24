# GitHub 首次上传

GitHub 不强制某一种源码目录格式。本仓库采用常见布局：根目录 README、许可证说明、第三方声明、源码与文档分目录、构建输出忽略、自动源码检查。

当前仓库：[YingXin0128/PF-MCS-freeRTOS](https://github.com/YingXin0128/PF-MCS-freeRTOS)。本地已绑定该仓库时，日常更新只需提交并推送，无需重复创建远程仓库。

## 上传前检查

在仓库根目录执行：

```powershell
python tools\verify_repository.py
git status --short
```

仅提交当前仓库中的源码、工程、文档和工具。`build/`、旧日志、个人 Keil 会话、仿真目标、资料安装包和个人文件均不应提交。原始 Keil HTML 日志可能包含本机信息或许可证信息，因此不上传原始日志。

## 创建远程仓库

在 GitHub 创建名为 **PF-MCS-freeRTOS** 的空仓库。描述可使用：

> 基于 STM32G431 与 FreeRTOS 的压力与流量监测控制系统，包含定点采集、PWM 步进控制、LCD 交互与延时告警。

建议 Topics：`stm32`、`stm32g4`、`freertos`、`embedded-c`、`pwm`、`pressure-control`、`flow-monitoring`、`keil`。

远程仓库初始化时不要额外创建 README 或许可证，以避免首次推送出现不必要的合并。公开前核对 `LICENSE` 与 `THIRD_PARTY_NOTICES.md` 中的来源和授权范围。

## 推送源码

在本仓库根目录执行；将示例 URL 的用户名替换为自己的 GitHub 用户名：

```powershell
# 如果目录尚未初始化 Git，先执行下一行
git init -b main
git add .
git diff --cached --stat
git commit -m "Initial hardware firmware release"
git remote add origin https://github.com/YOUR_GITHUB_NAME/PF-MCS-freeRTOS.git
git push -u origin main
```

如果 Git 尚未设置提交身份，请按自己的账户信息配置，不要使用示例身份代填。登录通过 GitHub 官方支持的 Git 凭据方式完成，不要将令牌写入仓库或 remote URL。

如已初始化或已绑定远程，先执行 `git status`、`git remote -v`，按实际情况跳过对应步骤。此文件中的 URL 是首次上传的命令示例，已有远程仓库时无需重复创建。

## 自动检查与后续更新

`.github/workflows/source-check.yml` 在推送和 PR 时执行仓库检查与主机 C 算法测试。GitHub Linux 环境运行的是软件检查，Keil ARMCC 5 固件全量编译仍需本地开发环境；工作流通过不代表已完成上板验收。

若需要分发固件，可在经过硬件验收后创建 GitHub Release，附上新生成的 HEX、对应源码版本、SHA-256 与实测条件。日常源码提交继续忽略构建目录。

源码修改后先本地全量构建，再运行 `python tools/record_build.py` 与 `python tools/test_algorithms.py --cc clang --report docs/data/software-tests.json` 更新两份结构化验证结果。它们保存源码哈希；仓库检查会拒绝与当前源码不一致的旧结果。该流程不会把原始日志加入 Git。
