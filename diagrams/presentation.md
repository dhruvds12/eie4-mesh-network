Below is a **20-minute slide deck blueprint** that balances clarity for examiners with enough depth to showcase the project’s technical sophistication. I’ve grouped slides into logical “chapters,” suggested timing, and pointed out where to reuse figures from your thesis (or create new visuals) for maximum impact.

| #                                                  | Section                             | Key messages & recommended visuals                                                                                                                  | Time     |
| -------------------------------------------------- | ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| 1                                                  | **Title & Context**                 | Project name, author, supervisors, date.                                                                                                            | 0:30 min |
| **Chapter 1 – Why this matters (Problem & Goals)** |                                     |                                                                                                                                                     |          |
| 2                                                  | The Communication Black-out Problem | One-slide infographic of real disaster stats (cell-tower outages, lives affected) to grab attention.                                                | 1:00     |
| 3                                                  | Project Vision & Objectives         | Bullet list of your three core aims (resilient mesh architecture, optimised routing, rich gateway services).                                        | 1:00     |
| **Chapter 2 – Design Overview**                    |                                     |                                                                                                                                                     |          |
| 4                                                  | System-at-a-Glance                  | Re-use Fig 4.1 “High level System Diagram”; animate data flow from phone → node → mesh → internet.                                                  | 1:30     |
| 5                                                  | Why LoRa?                           | Radar chart (adapted from Fig 2.1) contrasting LoRa, Zigbee, Wi-Fi, BLE on range, power, cost. Emphasise disaster-fit trade-offs.                   | 1:00     |
| 6                                                  | Hardware Node Stack                 | Photo of Heltec V3 board plus exploded call-outs (LoRa, Wi-Fi, BLE, MCU, battery).                                                                  | 1:00     |
| **Chapter 3 – Core Technical Contributions**       |                                     |                                                                                                                                                     |          |
| 7                                                  | Custom Reactive Routing Algorithm   | • Problem with flood vs proactive vs reactive.<br>• Your AODV-inspired design & optimisations (link-quality metric, TTL).<br>Mini-sequence diagram. | 2:00     |
| 8                                                  | End-to-End Security Model           | Layer cake showing AES-GCM payload, network “burnt-in” key, optional session keys; justify lightweight choice.                                      | 1:30     |
| 9                                                  | Internet Bridging Logic             | Gateway flow chart (adapt Fig 5.4). Explain packet-wrapping approach and two-way alerts to mesh.                                                    | 1:30     |
| 10                                                 | Mobile-App UX                       | 3 screenshots: chat thread, node-status, map. Highlight BLE pairing & offline-first DB.                                                             | 1:00     |
| **Chapter 4 – Verification & Results**             |                                     |                                                                                                                                                     |          |
| 11                                                 | Test Pipeline                       | Timeline graphic: unit → sim → HIL → field tests.                                                                                                   | 1:00     |
| 12                                                 | Simulation Results                  | Chart of PDR vs nodes (Fig 7.2) and latency vs hops (Fig 7.1). Summarise headline numbers.                                                          | 1:30     |
| 13                                                 | Field Test Snapshots                | Map with node locations, heat-coloured RSSI; photos of deployment.                                                                                  | 1:00     |
| 14                                                 | Battery Life Analysis               | Bar chart from Table 7.2; link back to low-power design choices.                                                                                    | 1:00     |
| 15                                                 | Requirements Scorecard              | Condensed version of Tables 8.1 & 8.2 with tick / partial / cross icons.                                                                            | 1:00     |
| **Chapter 5 – Impact & Future**                    |                                     |                                                                                                                                                     |          |
| 16                                                 | Societal Impact                     | Bullet highlights: community resilience, low-cost kits, open-source ecosystem.                                                                      | 0:45     |
| 17                                                 | Future Work Road-Map                | Three lanes: smarter routing, opportunistic high-speed links, production-grade hardware.                                                            | 0:45     |
| **Chapter 6 – Wrap-up**                            |                                     |                                                                                                                                                     |          |
| 18                                                 | Key Takeaways                       | 3-point summary + “thank you” message.                                                                                                              | 0:30     |
| 19                                                 | Q & A                               | Spare slide with project logo.                                                                                                                      | —        |

### How to pace it

*Aim for \~1 min per content slide; practice to trim or expand where needed.* Slides 7–14 carry the technical weight—ensure crisp visuals and limit text to one-line insights.

### Design tips

* **Visual dominance** > bullet overload. Wherever possible, turn tables/paragraphs from the thesis into icons, charts, or layered diagrams.
* **Progressive disclosure**: animate data paths and algorithm steps; it keeps attention and shows complexity without clutter.
* **Consistent colour coding**: e.g., blue for nodes, green for gateways, orange for phones, red for internet/cloud.
* **Backup slides**: keep extra graphs (simulation parameters, full spec tables) hidden after Q\&A in case examiners dig deeper.

### Next steps you can ask me for

1. Speaker-notes draft (≈150 words per slide).
2. Template with master colours & typography.
3. Auto-generated charts: supply CSV, and I’ll produce matplotlib plots.
4. Scripted PowerPoint export (I can build the `.pptx` via python\_user\_visible).

Let me know what would help you polish the deck!
