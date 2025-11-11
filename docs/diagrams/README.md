# Trading Engine Diagrams

This directory contains comprehensive UML and architectural diagrams for the trading engine service.

## ⚠️ Rendering Issues?

If the full sequence diagram (`sequence_diagram.puml`) is too large to render:

**Use the split diagrams instead:**
1. `test_simple.puml` - Minimal test (4 lines) to verify PlantUML works
2. `01_order_placement.puml` - Order placement hot path
3. `02_async_updates.puml` - WebSocket callbacks & fills
4. `03_publishing.puml` - Lock-free queue + ZMQ publishing

**Troubleshooting:**

| Issue | Solution |
|-------|----------|
| "Syntax error" | Try `test_simple.puml` first to verify setup |
| Online renderer timeout | Use split diagrams (01-03) or local renderer |
| VSCode not rendering | Install Java: `sudo apt install default-jre` |
| Blank output | Check PlantUML extension is installed & enabled |
| "Too complex" error | Increase memory: `PLANTUML_LIMIT_SIZE=8192` |

## Files

### Split Diagrams (Recommended)

#### `test_simple.puml`
**5-line test diagram** to verify PlantUML is working correctly.

#### `01_order_placement.puml`
**Order placement flow** - The hot path from client to exchange.
- Covers: Ingestion, parsing, validation, adapter call, REST API

#### `02_async_updates.puml`
**Async updates & fills** - WebSocket callbacks and lazy rehydration.
- Covers: Order updates, fill events, status normalization

#### `03_publishing.puml`
**Publishing flow** - Lock-free queue consumption and ZMQ multicasting.
- Covers: Report/fill serialization, queue operations, subscriber delivery

### Full Diagram (May be large)

#### `sequence_diagram.puml`
**PlantUML sequence diagram** showing detailed message flow between all components during order processing.

**Covers:**
- Order Placement Flow (synchronous hot path)
- Async Order Update Flow (WebSocket user stream)
- Fill Event Flow (trade execution)
- Publishing Flow (lock-free queue + ZMQ)

**To render:**

#### Online (Quick Preview)
1. Copy contents of `sequence_diagram.puml`
2. Paste into: https://www.plantuml.com/plantuml/uml/
3. View rendered diagram

#### VSCode Extension
1. Install: `PlantUML` extension (jebbs.plantuml)
2. Open `sequence_diagram.puml`
3. Press `Alt+D` or `Ctrl+Shift+P` → "PlantUML: Preview Current Diagram"

#### Command Line
```bash
# Install PlantUML
sudo apt-get install plantuml

# Generate PNG
plantuml sequence_diagram.puml

# Generate SVG (scalable)
plantuml -tsvg sequence_diagram.puml

# Generate PDF
plantuml -tpdf sequence_diagram.puml
```

### 2. `order_flow_activity.md`
**Mermaid activity diagram** showing the complete control flow from order ingestion to publishing.

**Features:**
- Branch-free action dispatch
- Error handling paths
- Memory pool management
- Lock-free queue operations
- Latency measurements

**To render:**

#### GitHub/GitLab
- Automatically rendered in markdown preview
- Viewable in web interface

#### VSCode Extension
1. Install: `Markdown Preview Mermaid Support`
2. Open `order_flow_activity.md`
3. Open markdown preview (`Ctrl+Shift+V`)

#### Command Line
```bash
# Install mermaid-cli
npm install -g @mermaid-js/mermaid-cli

# Generate PNG
mmdc -i order_flow_activity.md -o order_flow_activity.png

# Generate SVG
mmdc -i order_flow_activity.md -o order_flow_activity.svg -b transparent
```

### 3. `interaction_overview.md`
**Mermaid sequence diagram** with detailed interaction summary, performance metrics, and failure modes.

**Includes:**
- Component interaction summary with latencies
- Threading model details
- Interaction patterns (4 types)
- Performance bottlenecks analysis
- Failure modes & recovery strategies
- End-to-end latency budget (10-70 μs)
- Monitoring & observability metrics

**To render:** Same as `order_flow_activity.md` (Mermaid format)

## Key Differences Between Diagrams

| Diagram | Format | Best For | Detail Level | Rendering |
|---------|--------|----------|--------------|-----------|
| `test_simple.puml` | PlantUML | Testing setup | **Minimal** - 4 lines | ✅ Always works |
| `01_order_placement.puml` | PlantUML | Order hot path | **High** - Single flow | ✅ Fast |
| `02_async_updates.puml` | PlantUML | Async callbacks | **High** - WebSocket flow | ✅ Fast |
| `03_publishing.puml` | PlantUML | Publishing mechanism | **High** - Queue + ZMQ | ✅ Fast |
| `sequence_diagram.puml` | PlantUML | Complete system | **Highest** - All flows | ⚠️ May timeout |
| `interaction_overview.md` | Mermaid | High-level interactions | **High** - Component-level | ✅ GitHub/VSCode |
| `order_flow_activity.md` | Mermaid | Control flow & decisions | **Medium** - Logical flow | ✅ GitHub/VSCode |

## Recommended Viewing Order

For new developers understanding the system:

1. **Start with:** `order_flow_activity.md`
   - Understand the overall flow and decision points
   - See error handling paths
   - Grasp the threading model

2. **Then review:** `interaction_overview.md`
   - Learn component responsibilities
   - Understand performance characteristics
   - Study failure modes

3. **Deep dive:** `sequence_diagram.puml`
   - See exact method calls
   - Understand data structures
   - Follow async callback chains

## Integration with Documentation

These diagrams complement:
- `../system.md` - Comprehensive system report
- `../HYPERLIQUID_INTEGRATION.md` - Exchange-specific docs
- `../HFT_OPTIMIZATIONS.md` - Performance optimizations
- `../../README.md` - Project overview

## Diagram Maintenance

**When to update diagrams:**
- Adding new exchange adapters
- Modifying order processing logic
- Changing threading model
- Adding/removing callbacks
- Performance optimizations affecting flow

**Update checklist:**
- [ ] Update PlantUML sequence diagram (most detailed)
- [ ] Update Mermaid activity diagram (control flow)
- [ ] Update interaction overview (if patterns change)
- [ ] Verify latency measurements
- [ ] Update component descriptions
- [ ] Add notes for new edge cases

## Export Formats

### PlantUML Supported Formats
- PNG (default)
- SVG (recommended for docs)
- PDF (for printing)
- EPS (vector format)
- LaTeX (for papers)
- ASCII art (for terminals)

### Mermaid Supported Formats
- PNG
- SVG
- PDF

## Tips for Large Diagrams

The sequence diagram is comprehensive and may be large when rendered:

**For presentations:**
```bash
# Split into separate flows
plantuml -tsvg sequence_diagram.puml
# Then crop/edit SVG to show specific sections
```

**For documentation:**
- Use SVG format (scales without pixelation)
- Link to interactive PlantUML viewer
- Create separate diagrams per flow (place/cancel/replace)

**For code reviews:**
- Focus on changed sections
- Reference line numbers in .puml file
- Use ASCII art mode for terminal viewing

## Advanced PlantUML Features Used

- **Skinparam:** Custom theming
- **Activate/Deactivate:** Lifeline activation boxes
- **Alt/Else:** Conditional flows
- **Loop:** Repeated operations
- **Notes:** Inline documentation
- **Stereotypes:** Component classifications (`<<Socket>>`, `<<Thread>>`, etc.)
- **Grouping:** Logical sections with `== ... ==`

## Questions or Issues?

If diagrams don't match implementation:
1. Check git history for recent changes
2. Verify against actual code in `src/trading_engine_service.cpp`
3. Update diagrams and commit with clear message
4. Add notes explaining any architectural decisions

## Useful Links

- **PlantUML Language Reference:** https://plantuml.com/sequence-diagram
- **Mermaid Documentation:** https://mermaid.js.org/syntax/sequenceDiagram.html
- **UML Sequence Diagram Best Practices:** https://www.visual-paradigm.com/guide/uml-unified-modeling-language/what-is-sequence-diagram/
