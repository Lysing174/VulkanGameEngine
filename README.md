基于vulkan实现：Cook-Torrance BRDF的PBR直接光照、基于IBL的环境光照、方向光阴影贴图、基于ImGui的编辑器面板。

Stats 面板每 0.5 秒刷新一次：数量显示最近完成帧的场景统计，CPU/GPU 用时显示该采样区间内有效帧的平均值。

- 高斯数量按场景实例累计；高斯数据显存估算为 `场景高斯数 × sizeof(GaussianDataGPU)`（当前每个 64 字节），以 MiB 显示，不含排序 ping-pong、直方图、变换缓冲及分配对齐开销。后续建议另列包含这些资源的高斯渲染总显存，保留基础数据指标以便比较模型规模。
- 顶点数量按提交的 mesh 实例累计顶点缓冲中的顶点数，不是索引数量，不重复累计子网格、阴影 pass，不含天空盒、高斯 quad 或 ImGui。绘制高斯数和绘制顶点数暂用场景总数，`TODO(culling)` 标记了将来接入剔除结果的位置。
- 光源数统计提交的所有点光源与方向光（包括超过 GPU 光源上限的场景光源）。
- CPU work 包含事件处理、场景更新、资源提交、命令录制和 ImGui 构建，不含帧获取与呈现等待；资源重建本身的同步等待仍计入 CPU work。
- GPU 用时通过 Vulkan 时间戳查询分别测量阴影、mesh（含天空盒）、高斯排序（含深度键生成、直方图与排序同步）、高斯绘制、ImGui 绘制。读取上一帧可用结果，不为统计额外阻塞 GPU；未执行的阶段为零，不支持时间戳时显示 N/A。

GPU 计时采用 [Khronos 时间戳示例](https://github.khronos.org/Vulkan-Site/samples/latest/samples/api/timestamp_queries/README.html) 中的 TOP/BOTTOM 阶段组合，是近似的阶段用时；GPU 流水线可能重叠，不能把各项简单相加当作整帧 GPU 时间。

<img width="2558" height="1512" alt="image" src="https://github.com/user-attachments/assets/785eb871-2269-477c-ac7b-b1b476f73b76" />
