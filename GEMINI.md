我的毕设题目是《考虑单元库可行性分析的设计制造协同优化》（DTCO with Library Feasibility Analysis）。研究目标是在设计技术协同优化（DTCO）流程中实现对标准单元库的自动化可行性分析，通过优化标准单元的选择和使用策略，推动更高效的数字电路设计实现。本课题选择将研究重点聚焦在引脚可访问性分析上。

相关论文有但不限于：
1. TritonRoute-WXL: the open-source router with integrated DRC engine,
2. The tao of PAO: anatomy of a pin access oracle for detailed routing,
3. FastPass: A Fast Pin Access Analysis Framework for Detailed Routability Enhancement

ISPD19的信息：
1. 共10个测试。除了4和5使用65nm工艺，其他都使用32nm工艺（但是除了case9、10，其余case的工艺库都不一样）。

pae测试用的脚本在目录 pae_test/ 之下。

本课题的详细技术细节为：
PAE的技术设计文档在 @src/drt/doc/PAE_Design.md .

相关概念:
拆线重布（Rip-up and Reroute）：布线算法中用于解决布线资源冲突的一种迭代策略。在当前信号线无法找到合法路径时，布线工具会暂时移除部分已布好的、造成阻塞的信号线，然后重新规划所有受影响信号线的路径，以期找到全局可行的布线方案。
布线轨道（Routing Track）： 布线轨道是物理版图设计中基于特定金属层的设计规则定义的虚拟网格线。它们定义了金属互连线的中心在特定金属层上所允许放置的位置，是布线算法运行的基本网格。
引脚接入点（Pin Access Point）： 标准单元引脚几何图形上预先定义的、符合设计规则的特定坐标位置。布线工具必须通过通孔（Via）或金属线精确连接到这些点上，以实现电气导通。一个引脚可能有多个接入点。
独特单元实例（Unique Instance）：这是一种用于高效分析引脚可访问性的标准单元实例分类方法。两个单元实例若以下三个特征完全相同，则被视为同一种独特单元实例：
(i) 该单元实例对应的标准单元；
(ii) 放置方向，即单元实例是否发生了旋转或翻转；
(iii) 轨道偏移量，即单元实例相对于布线轨道的位置偏移。算法仅需针对每种独特单元实例进行分析，即可覆盖所有同类实例，从而显著减少降低计算复杂度。

接入模式（Access Pattern）： 针对一种独特单元实例，接入模式是指为其每个引脚选定一个接入点所构成的组合方案。该方案必须确保所有被选中的接入点之间满足设计规则（如间距要求），且彼此兼容，使得布线工具能够同时对它们进行连接。一种独特单元实例可能存在多个可行的接入模式。
引脚可访问性（Pin Accessibility）：在满足设计规则约束的前提下，标准单元的输入、输出端口可被布线工具成功连接的难易程度。

## 一、本课题要解决的技术问题
在先进工艺节点的数字集成电路设计中，设计制造协同优化已成为实现芯片良率与性能目标的关键方法论。标准单元库作为连接工艺制造与电路设计的核心桥梁，其设计质量直接决定了最终芯片的性能、功耗和面积（PPA）上限。然而，现有的技术机制缺乏对标准单元在实际电路设计中的可行性，特别是引脚可访问性的自动化、系统性评估机制。

具体而言，现有技术存在以下问题：
1. **评估滞后且依赖人工经验**：目前判断标准单元是否“好用”高度依赖后验式的分析，难以自动化和复用。
2. **缺乏系统性评价标准**：随着工艺微缩，引脚可访问性成为制约布线成功率的关键，但缺乏量化评价手段。

## 二、研究目标
本课题旨在提供一套可集成于芯片物理实现流程中的标准单元引脚可访问性量化评价方法及系统（PAE）。通过构建系统性的评价指标与自动化反馈机制，优化标准单元的选择与使用策略，从而减少迭代次数、缩短设计周期，并完成DTCO的反馈闭环。

## 三、系统架构 (PAE)
本课题实现为集成于 OpenROAD/TritonRoute 中的 PAE 模块。其工作过程遵循“静态特征提取-动态监控-综合评估-反馈指导”的闭环逻辑。

详细的技术实现方案、核心数据结构及指标计算方法请参考：`src/drt/doc/PAE_Design.md`。

## 四、预期效果与优点
1. **量化评价**：填补了标准单元引脚可访问性系统性量化评价的空白。
2. **自动化流程**：减少人工干预，提升评估效率。
3. **闭环优化**：为 DTCO 提供真实设计场景的反馈，指导工艺库与版图优化。

## 五、本课题的具体实现
我计划在开源eda工具openroad基础上，实现PAE。
third-party/abc，src/sta是submodule，我的项目应该不涉及对它们的改动。
尽可能保持原有对第三方库的依赖。
master分支用于保持与原仓库的同步。
main分支是我的毕设的主分支。
dev分支是我的日常开发分支。

你只能执行git命令中读取类命令(如git log)，不能执行写类命令(如git add, commit, push)。
你是在办公环境（win10）中。我常用git自带的bash。win的powershell太难用了，其不允许使用"&&"将两个命令相连。

办公环境和开发环境各有一个openroad仓库。两个repo源代码的最后一次commit是相同的。开发环境的openroad已完成配置并能编译。办公环境的代码commit需要同步至开发环境已进行编译、测试。

注意，只对标准单元进行引脚可访问性评价，不处理macro单元和IO pin/term。
我看了openroad/src/drt已有的实现代码，我观察到以下几点：
1. 相关的数据结构包括但不限于：frTerm, frBTerm, frMterm, frMaster, frInst, frInstTerm, frBPin, frMPin, frPin, frPinAccess, frAccessPoint, FlexPinAccessPattern, drPin, drAccessPattern, drNet, FlexPA, FlexDRWorker, FlexDR.
2. FlexPA原有简单的cost设计。可以在frAccessPoint::getCost和FlexPinAccessPattern::updateCost观察到其只采用了类似指标1的计算，
3. openroad在FlexPA::genAllAccessPoints生成全部接入点（ap），结果存储在frPin::aps_中（一个frPinAccess相当于一套接入点，frInst::pinAccessIdx_记录的是使用frPin::aps_的第几套接入点，因为多种独特单元实例可能对应同一个master）。
4. openroad在FlexPA::prepPatternInst为每个unique class生成pattern。(1)中间call了genPatternsInit，其把ap的cost注入到node中。(2)中间过程call了FlexPA::getEdgeCost。(3)结果存在FlexPA::unique_inst_patterns_；
5. openroad在prepPatternInstRows逐行、从左到右为每个inst确定access pattern。(1)中间call了genInstRowPatternInit，其把pattern的cost注入到node中。(2)中间过程call了FlexPA::getEdgeCost。(3)但是结果(当前inst最佳pattern)没有采用直观的存储方式（在每个inst存储其使用了哪个access pattern），而是采用了更有利于运行速度的方式（在frInstTerm::ap_直接存储其每个pin使用的frAccessPoint的指针）。但这种方式丧失了pattern的信息，对PAE评分不利。
6. 上述第4、5点采用了类似的动态规划算法。
7. TritonRoute::main()中执行完pa_->main后，随后处理或执行了gr guide、prep()、ta()，最后执行dr_->main()，进行实际的detailed route。我对FlexDR和FlexDRWorker的观察可能不详尽，请你注意深入挖掘代码，尊重你的判断。
8. FlexDRWorker在初始化阶段，call了initNet_term_Helper，该函数(1) 对trueterm（为frInstTerm或frBTerm）的每个pin的每个接入点，若其为上述观察5(3)点所存储的最佳接入点，则pin cost设为0，否则为1。(2) 将drAccessPattern存至对应drPin，将drPin存至对应drNet。由此完成FlexPA结果到FlexDR的转化。
9. FlexDRWorker::route_queue_main中先拆线，再布线
10. 模式信息在dr阶段丢失。FlexDR并不维护pattern这一整体概念，而只关心单个引脚的接入。
