# 安装依赖说明

请按照以下步骤重新安装依赖：

1. 删除现有的 node_modules 和锁文件（如果存在）：
   ```bash
   Remove-Item -Recurse -Force node_modules
   Remove-Item -Force package-lock.json -ErrorAction SilentlyContinue
   Remove-Item -Force pnpm-lock.yaml -ErrorAction SilentlyContinue
   ```

2. 使用 pnpm 安装依赖：
   ```bash
   pnpm install
   ```

3. 如果 electron 类型仍然找不到，可以尝试安装 @types/electron（虽然 electron 应该自带类型）：
   ```bash
   pnpm add -D @types/electron
   ```

4. 运行构建：
   ```bash
   pnpm run build
   ```

注意：镜像源已配置在 .npmrc 文件中，pnpm 会自动使用该镜像源。



