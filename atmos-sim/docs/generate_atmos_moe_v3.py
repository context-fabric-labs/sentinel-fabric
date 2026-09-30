from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path

from docx import Document
from docx.enum.section import WD_ORIENT, WD_SECTION
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt, RGBColor


DOCS_DIR = Path(__file__).resolve().parent
SOURCE = DOCS_DIR / "ATMOS_MoE_RackScale_Hybrid_E3S_Proposal_v2.docx"
OUTPUT = DOCS_DIR / "ATMOS_MoE_RackScale_Hybrid_E3S_Proposal_v3.docx"

NAVY = "17365D"
BLUE = "1F4E78"
PALE_BLUE = "DCE6F1"
PALE_GRAY = "F2F2F2"
WHITE = "FFFFFF"


def paragraph_text(element) -> str:
    return "".join(element.xpath(".//w:t/text()"))


def trim_from_paragraph(document: Document, text: str) -> None:
    body = document._element.body
    trimming = False
    for child in list(body.iterchildren()):
        if child.tag == qn("w:sectPr"):
            continue
        if child.tag == qn("w:p") and paragraph_text(child).strip() == text:
            trimming = True
        if trimming:
            body.remove(child)
    if not trimming:
        raise RuntimeError(f"Could not find trim marker: {text}")


def set_paragraph_text(paragraph, text: str) -> None:
    if paragraph.runs:
        paragraph.runs[0].text = text
        for run in paragraph.runs[1:]:
            run._element.getparent().remove(run._element)
    else:
        paragraph.add_run(text)


def replace_everywhere(document: Document, replacements: dict[str, str]) -> None:
    containers = list(document.paragraphs)
    for table in document.tables:
        for row in table.rows:
            for cell in row.cells:
                containers.extend(cell.paragraphs)
    for paragraph in containers:
        current = paragraph.text
        updated = current
        for old, new in replacements.items():
            updated = updated.replace(old, new)
        if updated != current:
            set_paragraph_text(paragraph, updated)


def set_cell_shading(cell, fill: str) -> None:
    properties = cell._tc.get_or_add_tcPr()
    shading = properties.find(qn("w:shd"))
    if shading is None:
        shading = OxmlElement("w:shd")
        properties.append(shading)
    shading.set(qn("w:fill"), fill)


def set_cell_text(cell, text: str, *, bold: bool = False, color: str | None = None,
                  size: float = 8.5) -> None:
    cell.text = ""
    paragraph = cell.paragraphs[0]
    paragraph.paragraph_format.space_after = Pt(0)
    run = paragraph.add_run(text)
    run.bold = bold
    run.font.size = Pt(size)
    if color:
        run.font.color.rgb = RGBColor.from_string(color)
    cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER


def add_table(document: Document, headers: list[str], rows: list[list[str]],
              *, font_size: float = 8.5) -> object:
    table = document.add_table(rows=1, cols=len(headers))
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = True
    for index, header in enumerate(headers):
        set_cell_text(table.rows[0].cells[index], header, bold=True, color=WHITE,
                      size=font_size)
        set_cell_shading(table.rows[0].cells[index], NAVY)
    for row_index, values in enumerate(rows):
        cells = table.add_row().cells
        for column_index, value in enumerate(values):
            set_cell_text(cells[column_index], value, size=font_size)
            if row_index % 2:
                set_cell_shading(cells[column_index], PALE_GRAY)
    document.add_paragraph()
    return table


def add_heading(document: Document, text: str, level: int = 1) -> None:
    paragraph = document.add_paragraph(style=f"Heading {level}")
    paragraph.paragraph_format.keep_with_next = True
    run = paragraph.add_run(text)
    run.font.color.rgb = RGBColor.from_string(BLUE)


def add_body(document: Document, text: str, *, bold_lead: str | None = None) -> None:
    paragraph = document.add_paragraph()
    paragraph.paragraph_format.space_after = Pt(6)
    if bold_lead and text.startswith(bold_lead):
        paragraph.add_run(bold_lead).bold = True
        paragraph.add_run(text[len(bold_lead):])
    else:
        paragraph.add_run(text)


def add_bullets(document: Document, items: list[str]) -> None:
    for item in items:
        paragraph = document.add_paragraph(style="List Bullet")
        paragraph.paragraph_format.space_after = Pt(3)
        paragraph.add_run(item)


def add_callout(document: Document, label: str, text: str) -> None:
    table = document.add_table(rows=1, cols=1)
    table.style = "Table Grid"
    cell = table.cell(0, 0)
    set_cell_shading(cell, PALE_BLUE)
    cell.text = ""
    paragraph = cell.paragraphs[0]
    paragraph.paragraph_format.space_after = Pt(0)
    label_run = paragraph.add_run(f"{label}  ")
    label_run.bold = True
    label_run.font.color.rgb = RGBColor.from_string(NAVY)
    paragraph.add_run(text)
    document.add_paragraph()


def add_page_break(document: Document) -> None:
    document.add_paragraph().add_run().add_break(WD_BREAK.PAGE)


def update_front_matter(document: Document) -> None:
    exact_updates = {
        "RACK-SCALE TARGET • RTX POC/MVP • MoE ONLY • SWITCHED E3.S":
            "GATED FEASIBILITY • TURNKEY OEM PATHS • MoE ONLY • SWITCHED E3.S",
        "ATMOS E3.S for MoE Serving":
            "ATMOS E3.S for MoE Serving — Version 3",
        "Hybrid expert acceleration for NVIDIA NVL and AMD Helios • RTX as POC/MVP reference":
            "Hybrid expert acceleration across rack OEMs • RTX as a development vehicle",
        "Part III — Insurance scenario: RTX MVP calibration and rack-scale translation":
            "Part III — Break-even model: illustrative RTX calibration and rack-scale translation",
    }
    for paragraph in document.paragraphs:
        if paragraph.text in exact_updates:
            set_paragraph_text(paragraph, exact_updates[paragraph.text])

    executive = (
        "This V3 proposal converts the V2 assessment into an evidence-gated feasibility "
        "program. The first decision is whether a complete GPU-router-to-ATMOS-expert-to-"
        "GPU continuation fits the measured per-layer latency budget; capacity fit alone is "
        "not sufficient. A single Qwen3-235B-class FP8 configuration is qualified first on "
        "an RTX development vehicle, followed by a four-GPU/four-ATMOS tray emulator and one "
        "named turnkey OEM rack path. NVIDIA NVL and AMD Helios remain reference platforms, "
        "while additional rack integrators are screened through the same attachment, "
        "availability, lifecycle, and measured-ROI gates."
    )
    for paragraph in document.paragraphs:
        if paragraph.text.startswith("This proposal defines a phased path"):
            set_paragraph_text(paragraph, executive)
        elif paragraph.text.startswith("Product target: Earn the product"):
            set_paragraph_text(
                paragraph,
                "Product target: Earn a product claim separately on each named OEM/platform; "
                "the RTX system is a software and heterogeneous-execution development vehicle."
            )
        elif paragraph.text.startswith("Validation discipline: Four grouped POCs"):
            set_paragraph_text(
                paragraph,
                "Validation discipline: Six sequential decision gates cover baseline freeze, "
                "primitive latency/service curves, tray-scale routing and faults, full RTX "
                "service, named OEM attachment, and independent rack-scale ROI qualification."
            )

    replace_everywhere(document, {
        "RTX POC/MVP": "RTX development vehicle",
        "POC / MVP": "RTX development vehicle",
        "RTX MVP": "RTX development vehicle",
        "MVP target": "development target",
        "MVP economic": "development economic",
        "MVP fixture": "development fixture",
        "MVP service": "development service",
        "MVP blocker": "development-vehicle blocker",
        "RTX-MVP": "RTX-development",
        "POC/MVP host": "development host",
        "POC/MVP engineering fixture": "development engineering fixture",
        "43% fewer servers and 120 fewer RTX GPUs in the target case":
            "Illustrative only: 35 vs 20 servers and 280 vs 160 RTX GPUs; not a product claim",
        "2,400 vs 3,500 units → ~31% lower":
            "Illustrative only: 2,400 vs 3,500 units; qualification required",
    })

    reading_map = document.tables[1]
    for row in reading_map.rows:
        if row.cells[0].text.strip() == "Part III":
            row.cells[1].text = "What measured economics would justify ATMOS?"
            row.cells[2].text = "Break-even equations; illustrative RTX numbers are not claims."
        elif row.cells[0].text.strip() == "Part IV":
            row.cells[0].text = "Part IV"
            row.cells[1].text = "Which turnkey partners should be engaged first?"
            row.cells[2].text = "Preliminary scored shortlist plus a common RFI gate."
    cells = reading_map.add_row().cells
    cells[0].text = "Part V"
    cells[1].text = "What evidence is needed before product commitment?"
    cells[2].text = "Six gated POCs with explicit pass, stop, pivot, owner, and evidence rules."

    topology = document.tables[5]
    cells = topology.add_row().cells
    cells[0].text = "Two balanced switch domains"
    cells[1].text = "Contains switch failure; reduces contention; can align to NUMA/root domains."
    cells[2].text = "Uses two upstream x16 paths and more board/power budget; must prove recovery."

    properties = document.core_properties
    properties.title = "ATMOS E3.S for Rack-Scale MoE Serving — V3"
    properties.subject = (
        "Evidence-gated MoE feasibility, failure reserve, turnkey OEM selection, and rack ROI"
    )
    properties.description = (
        "V3 integrates the V2 overall assessment into POC gates. Vendor scores are screening "
        "hypotheses, not measured ATMOS results or procurement commitments."
    )
    properties.revision = 6
    properties.modified = datetime.now(timezone.utc)

    for section in document.sections:
        for paragraph in section.footer.paragraphs:
            text = paragraph.text.replace("V2", "V3").replace("v2", "v3")
            if text != paragraph.text:
                set_paragraph_text(paragraph, text)


def add_review_traceability(document: Document) -> None:
    add_page_break(document)
    add_heading(document, "Part IV — V2 assessment disposition", 1)
    add_body(
        document,
        "V3 treats every assessment item as a decision obligation. An item is closed only by "
        "a versioned artifact and an approved result; prose, capacity arithmetic, or a vendor "
        "roadmap does not close a gate."
    )
    rows = [
        ["R1", "Per-MoE-layer serial round-trip latency", "G1, G3, G5",
         "Latency decomposition; p50/p95/p99; TPOT budget replay"],
        ["R2", "200 GB/s HBF and small-batch expert efficiency", "G1",
         "Measured expert service curves by shape, precision, and tokens/expert"],
        ["R3", "No exact rack attachment or lifecycle path", "G4",
         "Named OEM design record: lanes, mechanics, power, cooling, BIOS/BMC, service"],
        ["R4", "RTX results do not transfer to rack scale", "G4, G5",
         "RTX called a development vehicle; each target is requalified independently"],
        ["R5", "Insufficient failure reserve / incomplete model on loss", "G2, G3, G5",
         "Chosen availability policy; fault injection; capacity and TCO include reserve"],
        ["R6", "Assumption-driven 43% / 31% economics", "G0, G3, G5",
         "Break-even model populated only with accepted-throughput and quoted TCO evidence"],
        ["R7", "Top-8 fan-out, skew, and slowest-device tail", "G1, G2, G3",
         "Destination count, co-activation, imbalance, fan-in, and slowest contribution"],
        ["R8", "One switch is a shared failure domain", "G2, G4",
         "Single-domain versus dual-domain comparison; DPC/reset containment"],
        ["R9", "vLLM scope and long-term ownership understated", "G0, G3",
         "Three owned software boundaries, conformance tests, release/upstream plan"],
        ["R10", "Initial model/platform/transport matrix is too broad", "G0",
         "One checkpoint, precision, Gen5 switch, host-staged path, and pinned CUDA stack"],
    ]
    add_table(document, ["ID", "V2 assessment item", "Closure gate(s)", "Required evidence"], rows,
              font_size=8)
    add_callout(
        document,
        "CLAIM CONTROL",
        "No gate may be marked Pass with simulated timing when real endpoint timing is available. "
        "No RTX rate, vendor marketing claim, or capacity-fit result is transferable to a rack "
        "product claim."
    )


def add_vendor_landscape(document: Document) -> None:
    add_heading(document, "Part V — Turnkey AI vendor landscape", 1)
    add_body(
        document,
        "The ranking is a September 2026 screening hypothesis for ATMOS partnership priority, "
        "not a vendor performance benchmark. Public evidence establishes turnkey capability; "
        "only a returned RFI, quote, lab route, and POC can establish ATMOS compatibility or ROI."
    )
    add_body(
        document,
        "NVIDIA NVL and AMD Helios are reference accelerator/platform ecosystems and are not "
        "ranked here. The table ranks solution providers that could own the physical integration, "
        "validation, deployment, and lifecycle boundary around those or other accelerators."
    )
    add_heading(document, "Scoring method", 2)
    add_bullets(document, [
        "ROI potential (35%): plausible reduction in accelerator, rack, energy, network, or service cost for the fixed workload.",
        "Integration ease (30%): supported host PCIe route, mechanical/power/cooling access, firmware ownership, and lab availability.",
        "Flexibility (25%): form-factor, CPU/GPU, network, cooling, and OEM/ODM co-design choices without a proprietary GPU-stack modification.",
        "Lifecycle readiness (10%): turnkey rack validation, global deployment/support, spares, firmware, and one accountable escalation path.",
    ])
    add_body(
        document,
        "Each factor is scored 1–5. Weighted score = (35×ROI + 30×Ease + 25×Flexibility + "
        "10×Lifecycle) / 5. A score is invalidated if the vendor cannot return the mandatory "
        "G4 attachment artifacts."
    )

    section = document.add_section(WD_SECTION.NEW_PAGE)
    section.orientation = WD_ORIENT.LANDSCAPE
    section.page_width, section.page_height = section.page_height, section.page_width
    section.top_margin = Inches(0.55)
    section.bottom_margin = Inches(0.55)
    section.left_margin = Inches(0.55)
    section.right_margin = Inches(0.55)
    add_heading(document, "Preliminary ATMOS partner priority", 2)
    rows = [
        ["1", "Giga Computing GIGAPOD", "4.5", "4.5", "5.0", "4.0", "91.5", "Med",
         "Lead: explicit turnkey, NVIDIA/AMD/Intel, ORV3, air/DLC and bespoke facility integration"],
        ["2", "Supermicro DCBBS / SuperCluster", "4.5", "4.5", "4.5", "4.5", "90.0", "Med",
         "Lead: broad rack/form-factor portfolio, cooling, rack integration and L11/L12 validation"],
        ["3", "Penguin Solutions OriginAI", "4.5", "4.0", "4.5", "5.0", "88.0", "Med",
         "Lead: AMD/NVIDIA and broad component choice; tailored design-build-deploy-manage lifecycle"],
        ["4", "Lenovo Hybrid AI Factory / Neptune", "4.0", "3.5", "4.0", "4.5", "78.0", "Med",
         "Control: validated platforms, services and liquid cooling; internal PCIe/E3.S route unproven"],
        ["5", "Dell AI Factory", "4.0", "3.5", "3.5", "5.0", "76.5", "Med",
         "Control: NVIDIA turnkey plus AMD open architecture; validated-rack change control may constrain"],
        ["6", "Cisco Secure AI Factory", "3.5", "3.5", "4.0", "4.5", "74.5", "Med",
         "Parallel: full-stack AI PODs and Supermicro-enabled racks; tray ownership is shared"],
        ["7", "HPE Private Cloud AI / AI Factory", "3.5", "3.0", "3.0", "5.0", "67.5", "Med",
         "Parallel: strongest turnkey operations; appliance validation may limit low-level modification"],
    ]
    add_table(
        document,
        ["Rank", "Vendor / offer", "ROI", "Ease", "Flex", "Life", "/100", "Conf.", "Recommended action"],
        rows,
        font_size=6.9,
    )
    add_callout(
        document,
        "SHORTLIST DECISION",
        "Engage ranks 1–3 for the fastest co-design learning, and one of ranks 4–5 as the "
        "enterprise lifecycle control. Run the same RFI and G4 evidence gate for every vendor; "
        "do not select a production partner from this paper score."
    )

    section = document.add_section(WD_SECTION.NEW_PAGE)
    section.orientation = WD_ORIENT.PORTRAIT
    section.page_width, section.page_height = section.page_height, section.page_width
    section.top_margin = Inches(0.7)
    section.bottom_margin = Inches(0.7)
    section.left_margin = Inches(0.7)
    section.right_margin = Inches(0.7)
    add_heading(document, "Alternative full-stack systems", 2)
    add_body(
        document,
        "Cerebras, Groq, and SambaNova offer integrated AI systems, but their proprietary "
        "accelerator/runtime paths do not currently expose a public, supported ATMOS E3.S "
        "layer-synchronous MoE insertion point. Intel Gaudi systems sold through OEMs remain a "
        "watchlist route: standard Ethernet and OEM servers may help infrastructure economics, "
        "but SynapseAI and serving integration create a new software qualification branch."
    )
    add_table(document, ["Route", "ATMOS posture", "Decision"], [
        ["Cerebras / Groq / SambaNova appliances",
         "Potential asynchronous retrieval/data service only; synchronous expert offload is unproven and may duplicate native architecture.",
         "Do not fund in the first POC wave; require a vendor-supported device/runtime extension point."],
        ["Intel Gaudi through a rack OEM",
         "Potential host-PCIe or companion path, but requires a separate framework, transport, and baseline economics branch.",
         "Revisit after G3 if a customer/OEM supplies a named platform and funded use case."],
    ], font_size=8)
    add_heading(document, "ODM and regional watchlist", 2)
    add_body(
        document,
        "QCT/Quanta, Foxconn/Ingrasys, Wiwynn, and ASUS publicly participate in rack-scale AI "
        "infrastructure. They are not scored in V3 because public material reviewed here does "
        "not establish all three of: a directly procurable turnkey offer, a named ATMOS co-design "
        "owner, and an enterprise field-support boundary. Invite them through the same RFI when "
        "commercial reach, geography, customer pull, or an existing relationship justifies it."
    )


def add_gated_program(document: Document) -> None:
    add_heading(document, "Part VI — Gated POC execution plan", 1)
    add_body(
        document,
        "Gates are sequential for product claims. G4 vendor discovery begins in parallel after "
        "G0, but no custom rack engineering is funded until G1 passes. Every pass requires the "
        "named evidence package, independent review, and a compatibility-manifest revision."
    )
    summary_rows = [
        ["G0 / POC-A", "Freeze model, SLO, oracle, software, hardware, cost inputs", "R6, R9, R10", "Baseline owner"],
        ["G1 / POC-B", "Primitive round trip and single-device expert service curves", "R1, R2, R7", "Performance + NPU"],
        ["G2 / POC-C", "Four-device tray emulator: placement, fan-out, switch and faults", "R5, R7, R8", "Platform validation"],
        ["G3 / POC-D", "8+8 RTX complete service, maintained vLLM seam, availability, economics", "R1, R4–R7, R9", "Runtime + Product"],
        ["G4 / POC-E", "Named turnkey OEM physical/electrical/lifecycle attachment", "R3, R4, R8", "OEM + Sandisk"],
        ["G5 / POC-F", "Independent rack service and ROI qualification", "All", "Joint qualification"],
    ]
    add_table(document, ["Gate / POC", "Decision", "Assessment coverage", "Accountable owner"], summary_rows,
              font_size=8)

    gate_details = [
        (
            "G0 / POC-A — Freeze the first qualified configuration",
            [
                "Entry: one Qwen3-235B-A22B-class checkpoint; one qualified FP8 format; host-staged transport; one Gen5 switch profile; 8 RTX + 8 ATMOS target fixture; one pinned vLLM/PyTorch/CUDA/driver/OS build.",
                "Execute: capture a reproducible GPU-only oracle and latency decomposition; freeze tokenizer, routing/top-k, quality set, TTFT/TPOT SLO, request mix, accepted-throughput definition, load points, power method, and three-year TCO boundary.",
                "Software contract: name owners and conformance suites for (1) Expert Execution ABI, (2) ATMOS Transport Runtime, and (3) vLLM Integration Layer; record release cadence and upstream/fork strategy.",
                "Pass: the baseline reproduces within the predeclared tolerance across three runs; the per-layer ATMOS budget, burst reserve, quality tolerance, and cost hurdle are signed before hybrid measurements.",
                "Stop/pivot: no reproducible baseline, unsupported FP8/expert operator, or no maintainable integration owner. Narrow operator/precision or select a different checkpoint before hardware fan-out work.",
                "Evidence: configuration manifest, hashes, baseline traces, numerical oracle, latency-budget worksheet, workload manifest, cost model, and ownership/RACI."
            ]
        ),
        (
            "G1 / POC-B — Fundamental latency and expert execution",
            [
                "Entry: real GPU, real ATMOS endpoint/NPU path, compiled real expert shapes, synchronized clocks/tracing, and G0 budget.",
                "Execute: measure GPU dispatch through ATMOS execution and GPU continuation for top-1/top-2/top-8; 1/2/4/8 destinations; 1/2/4/8/16/32/64 tokens per expert; warm and loaded queues; host-staged and any documented peer path; p50/p95/p99.",
                "Service curves: report useful HBF bandwidth, NPU utilization, weight/activation bytes, queue depth, arrival/service rate, power, precision error, and latency by expert shape and tokens/expert.",
                "Pass: p99 complete continuation fits the signed per-layer budget and replayed end-to-end TPOT stays within SLO at the target accepted load; the service curve sustains trace-derived peak demand plus the G0 burst reserve without unbounded queue growth.",
                "Stop/pivot: both supported transports miss the budget or small-batch service cannot meet demand. Test layer grouping, router/combine relocation, larger batches, prefill/offline-only use, or GPU-resident hot experts before any rack investment.",
                "Evidence: raw traces, service-curve dataset, numerical comparison, queue model, synchronization audit, and signed feasibility report."
            ]
        ),
        (
            "G2 / POC-C — Four-device tray emulator and fault domains",
            [
                "Entry: four ATMOS devices behind one balanced x16-to-4×x4 Gen5 domain, representative host/GPU traffic, and G1-qualified primitive.",
                "Execute: replay router traces under one and four concurrent GPUs; measure average/p95 destinations per token/layer, co-activation, slowest contribution, fan-in, queue imbalance, head-of-line blocking, and placement-epoch changes.",
                "Fault matrix: endpoint removal, malformed completion, link degradation, DPC/AER, switch firmware/reset, downstream FLR, host re-enumeration, and power/thermal throttling. Compare one eight-device domain with two balanced four-device domains before G3/G4 topology lock.",
                "Availability rule: select and cost one correctness-preserving policy—two complete service groups by default; N+1 rebuild, GPU fallback, or cross-node replicas only if measured RTO/SLO and retained capacity pass.",
                "Pass: no silent quality error; bounded p99 and queues at the G0 load; a failed endpoint/domain is contained; unaffected service continues according to the declared policy; reset/rejoin meets the signed RTO/RPO.",
                "Stop/pivot: tail latency, fan-out, reset blast radius, or reserve cost violates the signed envelope. Re-place/replicate experts, split switch domains, reduce ATMOS destinations, or narrow the service class.",
                "Evidence: topology/NUMA map, placement report, fault-injection log, recovery trace, availability-capacity calculation, and single-vs-dual-domain decision."
            ]
        ),
        (
            "G3 / POC-D — Complete RTX development service",
            [
                "Entry: G0–G2 pass; 8 RTX + 8 ATMOS development vehicle; complete ExpertPack; versioned vLLM integration; chosen failure policy.",
                "Execute: A/B GPU-only and hybrid at identical checkpoint, quality, request mix, TTFT/TPOT, failure reserve, and offered-load sweep. Exercise continuous batching, cancellation, timeout, backpressure, rolling update/rollback, prefix caching, speculative decoding policy, and degraded operation.",
                "Pass: quality is within G0 tolerance; accepted service rate within SLO exceeds the cost-adjusted break-even rate; no unbounded queue; the chosen failure policy preserves correctness; three-run confidence intervals and power are reported.",
                "Stop/pivot: capacity fits but service rate, SLO, quality, maintainability, or fully burdened economics fail. Do not infer rack value; narrow to prefill/offline/cold-expert service or stop fine-grained offload.",
                "Evidence: reproducible deployment bundle, compatibility manifest, A/B dataset, fault/upgrade report, software maintenance plan, BOM/quote, and populated break-even workbook."
            ]
        ),
        (
            "G4 / POC-E — Named turnkey OEM attachment",
            [
                "Entry: G1 pass; vendor NDA/RFI; one exact orderable rack/tray; named OEM engineering and support owners. Discovery may start after G0, but custom NRE starts only after G1.",
                "Execute: choose OEM-integrated local subsystem first, approved companion PCIe tray second; network-attached ATMOS is excluded from layer-synchronous MoE and may be assessed only for asynchronous services.",
                "Required return: exact connector/root/lanes/NUMA route; module and switch mechanics; cable/retimer budget; power/cooling; BIOS/IOMMU/ACS/DPC; BMC/update/reset; host architecture; peer-DMA status; service procedure; spares; warranty; quote; NRE; roadmap; lab access.",
                "Pass: OEM signs a supported design record with no proprietary GPU firmware/driver patch, supplies a measurable route and lifecycle owner, and the modeled latency/power/cost remain inside G1/G3 envelopes.",
                "Stop/pivot: no supported route, inaccessible reset/IOMMU policy, no thermal/mechanical envelope, or no accountable field support. Try companion tray or next ranked vendor; do not treat spare storage bays as compatibility.",
                "Evidence: signed RFI/design record, CAD/lane/NUMA diagrams, thermal/power model, firmware and service contract, risk register, quote, and qualification schedule."
            ]
        ),
        (
            "G5 / POC-F — Independent rack-scale product and ROI qualification",
            [
                "Entry: G3 and G4 pass for one named platform; production-intent hardware/software; GPU-only control rack; chosen availability reserve deployed on both arms.",
                "Execute: repeat the complete workload, offered-load sweep, quality, TTFT/TPOT, placement, faults, updates, power, cooling, operations, and cost study. Re-tune only through a documented procedure available to both control and hybrid arms.",
                "Economic metric: three-year TCO per accepted token/request within SLO includes accelerators, ATMOS, switches, rack/network, energy/cooling, licenses, support, spares, failure reserve, and amortized NRE. Report ROI and sensitivity; do not reuse RTX ratios.",
                "Pass: hybrid beats the G0 hurdle at the same SLO/quality/availability, with a supported lifecycle, positive sensitivity over approved demand/cost ranges, and no critical unresolved fault or security item.",
                "Stop/pivot: no product claim on the failing vendor/platform. Preserve any successful platform-specific release or narrow to an asynchronous/prefill workload with a new baseline.",
                "Evidence: independent qualification report, raw benchmark package, accepted-load curves, three-year TCO/ROI workbook, support readiness review, security review, and release compatibility manifest."
            ]
        ),
    ]
    for title, bullets in gate_details:
        add_heading(document, title, 2)
        add_bullets(document, bullets)

    add_heading(document, "Execution controls", 2)
    add_table(document, ["Control", "Rule"], [
        ["Gate authority", "Architecture, performance, product/finance, and reliability owners sign; the workstream owner cannot self-approve."],
        ["Result states", "Pass, Conditional Pass with dated closure, Pivot, or Stop. 'Promising' is not a decision state."],
        ["Reproducibility", "Publish manifests, hashes, raw traces, analysis code, run logs, confidence intervals, and deviations with each result."],
        ["Claim hygiene", "Label simulation, modeled values, vendor claims, targets, and measurements distinctly in every chart and executive summary."],
        ["Change control", "Checkpoint, precision, topology, driver, firmware, vLLM, SLO, workload, or failure policy changes invalidate affected gates."],
    ], font_size=8)


def add_decision_and_sources(document: Document) -> None:
    add_heading(document, "Recommended program decision", 1)
    add_callout(
        document,
        "RECOMMENDATION",
        "Approve G0 and G1 plus parallel no-NRE OEM discovery. Do not approve a production "
        "rack direction, eight-device custom switch investment, or headline ROI until the "
        "primitive latency/service-curve gate passes."
    )
    add_bullets(document, [
        "Use Qwen3-235B-class FP8, host staging, Gen5, and one pinned NVIDIA software build as the first qualification path.",
        "Build the four-GPU/four-ATMOS tray emulator after G1; compare one and two switch failure domains before topology lock.",
        "Treat two complete service groups as the conservative availability baseline until a cheaper N+1/GPU-fallback policy proves correctness and RTO.",
        "Issue the common G4 RFI first to Giga Computing, Supermicro, Penguin Solutions, and one enterprise OEM control (Lenovo or Dell).",
        "Qualify each rack vendor/platform independently; publish no aggregate 'NVIDIA', 'AMD', or 'turnkey OEM' ROI claim."
    ])
    add_heading(document, "Public source register", 1)
    add_body(
        document,
        "Accessed 30 September 2026. Sources establish public product positioning only; they "
        "do not establish ATMOS compatibility. Exact product availability, configuration, and "
        "commercial terms require vendor confirmation."
    )
    sources = [
        ["Supermicro", "NVIDIA Blackwell systems and Data Center Building Block Solutions", "https://www.supermicro.com/en/accelerators/nvidia"],
        ["Giga Computing", "GIGAPOD turnkey rack-scale AI infrastructure", "https://www.gigabyte.com/Solutions/gigapod"],
        ["Penguin Solutions", "OriginAI complete AI factory infrastructure", "https://www.penguinsolutions.com/solutions/ai/originai"],
        ["Lenovo", "Hybrid AI solutions / Hybrid AI Factory", "https://www.lenovo.com/us/en/servers-storage/solutions/ai/"],
        ["Dell", "Dell AI Factory; NVIDIA turnkey and AMD open paths", "https://www.dell.com/en-us/dt/solutions/artificial-intelligence/index.htm"],
        ["Cisco", "Secure AI Factory with NVIDIA", "https://www.cisco.com/site/us/en/solutions/artificial-intelligence/secure-ai-factory/index.html"],
        ["HPE", "HPE Private Cloud AI and AI Factory solutions", "https://www.hpe.com/us/en/ai-factory.html"],
        ["NVIDIA", "DGX GB300 / GB300 NVL72 platform documentation", "https://docs.nvidia.com/dgx/dgxgb300-user-guide/introduction-to-dgX-gb300.html"],
        ["AMD", "Helios rack-scale AI architecture", "https://www.amd.com/en/corporate/events/advancing-ai.html"],
    ]
    add_table(document, ["Vendor", "Source scope", "URL"], sources, font_size=7.5)
    add_heading(document, "V3 revision record", 2)
    add_table(document, ["Revision", "Date", "Material change"], [[
        "V3", "30 September 2026",
        "Integrated all V2 assessment items into traceable gates; added failure reserve, "
        "dual-domain topology comparison, software ownership, named OEM RFI, and preliminary "
        "turnkey-vendor ranking. Removed the appended review narrative."
    ]], font_size=8)


def validate(document: Document) -> None:
    full_text = "\n".join(p.text for p in document.paragraphs)
    for required in [
        "Part IV — V2 assessment disposition",
        "Part V — Turnkey AI vendor landscape",
        "Part VI — Gated POC execution plan",
        "G0 / POC-A",
        "G5 / POC-F",
        "Supermicro DCBBS / SuperCluster",
        "Recommended program decision",
    ]:
        if required not in full_text and not any(
            required in cell.text
            for table in document.tables
            for row in table.rows
            for cell in row.cells
        ):
            raise RuntimeError(f"Missing required V3 content: {required}")
    if "Overall assessment" in full_text:
        raise RuntimeError("The appended V2 assessment was not removed")
    if len(document.tables) < 34:
        raise RuntimeError(f"Expected at least 34 tables, found {len(document.tables)}")


def main() -> None:
    if not SOURCE.exists():
        raise FileNotFoundError(SOURCE)
    document = Document(SOURCE)
    trim_from_paragraph(document, "EVIDENCE SEQUENCE")
    update_front_matter(document)
    add_review_traceability(document)
    add_vendor_landscape(document)
    add_gated_program(document)
    add_decision_and_sources(document)
    validate(document)
    document.save(OUTPUT)
    reopened = Document(OUTPUT)
    validate(reopened)
    print(
        f"Wrote {OUTPUT} | paragraphs={len(reopened.paragraphs)} "
        f"tables={len(reopened.tables)} sections={len(reopened.sections)}"
    )


if __name__ == "__main__":
    main()