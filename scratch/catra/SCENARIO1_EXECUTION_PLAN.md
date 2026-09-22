# CATRA Scenario 1 — Kế hoạch thực hiện từng bước trên ns-3.47

## 1. Mục tiêu và phạm vi

Tài liệu này chuyển kế hoạch nghiên cứu tổng quát thành thứ tự triển khai cụ thể
cho checkout ns-3.47 hiện tại. Mục tiêu là tái tạo Scenario 1 theo từng lớp có
thể kiểm chứng:

```text
Topology/PHY
    -> deterministic routing
    -> Original TCP baseline
    -> measurement
    -> CATRA station state
    -> CATRA MAC
    -> CATRA flow state
    -> CATRA TCP
    -> full CATRA comparison
```

Không triển khai CATRA MAC và CATRA TCP đồng thời. Mỗi bước phải tạo được bằng
chứng máy đọc được và vượt acceptance gate trước khi bước kế tiếp bắt đầu.

Kết quả cuối cùng cần so sánh tối thiểu:

```text
n = 3, 4, 5, 6
mode = baseline, catra-mac, catra-full
```

với throughput từng flow, tổng end-to-end throughput, hop-weighted throughput
và Jain fairness index.

## 2. Trạng thái hiện tại của repository

### 2.1. Đã có

- `scratch/catra/README.md` là source of truth về paper parameters, porting
  choices và acceptance gates.
- `scratch/catra/catra-phy-range-probe.cc` đã định nghĩa phép kiểm tra quan hệ
  decode/CCA tại 200, 250, 400, 550 và 600 m.
- `scratch/catra/catra-scenario1.cc` đã dựng chain topology `n=3..6`, calibrated
  802.11b PHY, địa chỉ IPv4 và kiểm tra invariant Phase 2.
- `scratch/catra/active-time-estimation/` đã có implementation đọc-only của
  Algorithm 1, gồm packet parser, MAC observer, transaction tracker và
  active-time estimator.
- `scratch/catra/CMakeLists.txt` đã khai báo ba executable hiện tại.

### 2.2. Chưa có trong Scenario 1 executable

- Static host routes cho forward và reverse direction.
- UDP route-validation traffic.
- Hai saturated TCP flows và hai sinks.
- Baseline throughput/FlowMonitor CSV.
- `nSEND`, `nTX`, `nCS`, `ntotal`, `FBRS`.
- Tích hợp Algorithm 1 estimator vào từng station của Scenario 1.
- CATRA MAC CW controller.
- CATRA TCP per-flow estimator/controller.
- Experiment runner và Fig. 4 plotting pipeline.

### 2.3. Ranh giới quan trọng

- Paper dùng TCP Tahoe; checkout hiện tại dùng `TcpNewReno` làm baseline port.
  Không được gọi kết quả này là strict Tahoe reproduction.
- Algorithm 1 hiện chỉ đo active time và `RBRs`; không được suy ra rằng flow
  counting, CW control hoặc CATRA TCP đã được triển khai.
- Giá trị CW của NS-2 và ns-3 có thể khác quy ước. Chưa được gán `32/1024` trực
  tiếp cho ns-3 trước khi hoàn tất bước mapping.

## 3. Quy ước triển khai

### 3.1. Mode

Scenario executable phải hỗ trợ dần các mode sau:

```text
topology       # topology-only, tương đương trạng thái hiện tại
route-probe    # static routes + UDP validation
baseline       # Original TCP/NewReno, CATRA disabled
measure-only   # baseline + CATRA read-only measurements
catra-mac      # measurement + station CW control
catra-full     # CATRA MAC + CATRA TCP
```

### 3.2. CLI ổn định

Giữ các option PHY hiện tại và bổ sung:

```text
--mode=baseline
--n=5
--simTime=300
--trafficStart=1
--ep=2
--highTh=1.05
--lowTh=0.7
--seed=1
--run=1
--outputDir=results/catra/scenario1
--enablePcap=false
--traceRoutes=false
--traceMac=false
--traceTcp=false
--verboseCatra=false
--strict=true
```

Tên và semantics của option phải được khóa trước khi viết experiment runner.

### 3.3. Output ownership

```text
results/catra/scenario1/
├── throughput.csv
├── station-state.csv
├── flow-state.csv
├── metadata.csv
└── logs/
```

CSV chỉ chứa record máy đọc được. Diagnostic log đi ra stdout/stderr hoặc file
log riêng, không trộn vào CSV.

## 4. Thứ tự thực hiện chi tiết

## Step 0 — Revalidate nền tảng hiện có

### Mục tiêu

Xác nhận Phase 1, Phase 2 và Algorithm 1 probe vẫn build/run được trước khi mở
rộng Scenario 1.

### Công việc

1. Build ba target hiện có.
2. Chạy strict PHY range probe.
3. Chạy `catra-scenario1` với `n=3,4,5,6`.
4. Chạy strict active-time probe.
5. Lưu command và kết quả PASS vào README hoặc test log.

### Lệnh validation dự kiến

```bash
docker compose exec -T ns3 ./ns3 build \
  catra-phy-range-probe catra-scenario1 catra-active-time-probe -j 2

docker compose exec -T ns3 ./ns3 run catra-phy-range-probe --no-build

for n in 3 4 5 6; do
  docker compose exec -T ns3 ./ns3 run \
    "catra-scenario1 --n=${n} --strict=true --printTopology=false" --no-build
done

docker compose exec -T ns3 ./ns3 run catra-active-time-probe --no-build
```

### Acceptance gate

- 200/250 m decode, 400/550 m CCA-only, 600 m no detection.
- Tất cả topology invariant của `n=3..6` PASS.
- Algorithm 1 strict probe PASS, gồm MAC-ACK correlation.
- Không có CATRA controller làm thay đổi baseline.

## Step 1 — Hoàn tất source mapping cho routing, MAC và TCP hooks

### Mục tiêu

Ghi rõ API/source-of-truth trước khi sửa protocol state.

### File tạo mới

```text
scratch/catra/NS3_SOURCE_MAPPING.md
```

### Nội dung bắt buộc

| Paper concept | ns-3 owner | Function/trace | Read/write | Unit |
|---|---|---|---|---|
| current CW | `Txop`/DCF owner | exact API | read | slots |
| new backoff | exact class | exact function | write hook | slots |
| TX success/failure | exact trace | callback | read | event |
| PHY/MAC active time | existing observer | trace callbacks | read | ns |
| TCP cwnd | `TcpSocketState` | trace/member | read/write | bytes |
| ssthresh | `TcpSocketState` | trace/member | read/write | bytes |
| bytes in flight | socket state/trace | exact API | read | bytes |
| highest ACK/current sequence | exact owner | exact API | read | sequence bytes |
| next packet generation | application/socket owner | exact hook | write | time |

### Công việc

1. Map CW update/reset/backoff generation trong `src/wifi/model`.
2. Xác nhận CW values của ns-3 là inclusive slot range kiểu `2^k-1` hay cách
   biểu diễn khác; ghi mapping paper `32/1024` thành quyết định riêng.
3. Map NewReno `IncreaseWindow`, slow start, congestion avoidance, loss/RTO.
4. Xác định hook tối thiểu để CATRA TCP thay đổi `cwnd` và delay mà không phá
   recovery state machine.
5. Ghi rõ API nào chỉ dùng quan sát và API nào dự kiến dùng điều khiển.

### Acceptance gate

Không còn ô `TBD` cho current CW, backoff, success/failure, cwnd, bytes in
flight và packet-generation delay. Mọi unit đều rõ ràng.

## Step 2 — Static routes và bidirectional UDP route probe

### Mục tiêu

Buộc Flow 2 đi qua đúng `n-1` hop và bảo đảm reverse path hoạt động trước khi
thêm TCP.

### Thay đổi dự kiến

- Tách helper topology/routing khỏi `catra-scenario1.cc` nếu file bắt đầu quá
  lớn; chưa cần chuyển vào `contrib/`.
- Thêm `/32` host route hướng tới `R` trên từng station.
- Thêm reverse `/32` host route từ `R` và relay về từng source.
- Thêm mode `route-probe` với UDP sequence-tagged packets.
- Trace IPv4 send/forward/local-deliver/drop theo node và direction.

### Test matrix

```text
n = 3,4,5,6
forward: S2 -> R
reverse: R -> S2
short:   S1 -> R
```

### Acceptance gate

- Long forward và reverse path có đúng `n-1` transmissions.
- Short flow có đúng 1 hop.
- Distant destination không bị gửi trực tiếp do cùng subnet.
- Không có route discovery/convergence dependency.

## Step 3 — Original TCP/NewReno baseline

### Mục tiêu

Tạo hai flow bão hòa đồng thời nhưng chưa có bất kỳ CATRA measurement/control
nào tác động lên protocol.

### Cấu hình

```text
Flow 1: S1 -> R, TCP port 5001, one hop
Flow 2: S2 -> R, TCP port 5002, n-1 hops
Application: BulkSend, MaxBytes=0
Sink: two PacketSink instances
SendSize/segment target: 1024 bytes
Traffic start: 1 s
Simulation stop: 300 s
Active measurement duration: 299 s
TCP: TcpNewReno, explicitly labeled PORT
```

### Instrumentation tối thiểu

- Sink received bytes per flow.
- FlowMonitor cross-check.
- TCP `cwnd`, retransmissions và socket state khi trace flag bật.
- MAC queue full vs lifetime-expiry drops.
- Short PCAP option để chứng minh RTS/CTS/DATA/ACK.

### Acceptance gate

- Hai flow truyền dữ liệu đồng thời.
- Flow 1 và Flow 2 được phân biệt chắc chắn bằng port/5-tuple.
- Sink counters và FlowMonitor chênh lệch chỉ bởi semantics đã giải thích.
- PCAP ngắn cho thấy RTS/CTS/DATA/ACK và đúng hop path.
- `baseline` không khởi tạo CATRA controller.

## Step 4 — Throughput metrics và CSV contract

### Mục tiêu

Khóa pipeline đo baseline trước khi thêm estimator.

### CSV chính

```text
mode,n,seed,run,tcp,active_s,
flow1_mbps,flow2_mbps,total_e2e_mbps,hop_weighted_mbps,jain,
flow1_rx_bytes,flow2_rx_bytes
```

### Công thức

```text
flow_i_mbps = rx_bytes_i * 8 / active_s / 1e6
total_e2e_mbps = flow1_mbps + flow2_mbps
hop_weighted_mbps = flow1_mbps + (n - 1) * flow2_mbps
jain = (flow1 + flow2)^2 / (2 * (flow1^2 + flow2^2))
```

Không gọi `hop_weighted_mbps` là channel throughput nếu chưa đo trực tiếp
channel payload/time.

### Deterministic tests

1. Test công thức với input byte counters cố định.
2. Test zero-throughput để không chia cho zero.
3. Test CSV header/order và một row parse lại được.
4. Test active duration là `stop-start`, không phải toàn simulation time.

### Acceptance gate

CSV không phụ thuộc verbose log và deterministic metric tests PASS.

## Step 5 — Baseline matrix `n=3..6`

### Mục tiêu

Chứng minh topology hiện tại tạo ra vấn đề long-hop/short-hop trước CATRA.

### Trình tự

1. Smoke run ngắn cho `n=3..6`, seed/run cố định.
2. Full 300 s cho `n=3..6`, tối thiểu 3 runs để phát hiện instability.
3. Khi pipeline ổn định, tăng lên 10–30 runs/configuration.
4. Tổng hợp mean, standard deviation và confidence interval.

### Acceptance gate

- Khi `n` tăng, Flow 2 suy giảm rõ rệt hoặc bất lợi so với Flow 1 được giải
  thích bằng trace.
- Nếu xu hướng không xuất hiện, dừng và kiểm tra PHY/routing/queue/TCP; không
  điều chỉnh CATRA để ép kết quả.
- Metadata ghi đủ resolved TCP/queue/PHY values.

## Step 6 — Flow identity và flow-counting model

### Mục tiêu

Tính đúng `nSEND`, `nTX`, `nCS`, `ntotal` độc lập với active-time estimator.

### Data model đề xuất

```cpp
struct CatraFlowId
{
    Ipv4Address source;
    Ipv4Address destination;
    uint16_t sourcePort;
    uint16_t destinationPort;
};

struct CatraStationFlowCounts
{
    uint32_t nSend;
    uint32_t nTx;
    uint32_t nCs;
    uint32_t nTotal;
};
```

TCP ACK thuộc flow gốc, không tạo competing flow mới.

### Tách logic

1. Flow registry biết source, destination và route.
2. Geometry/radio relationship map biết node nào thuộc decode range và
   CCA-only range.
3. Counter thuần tính `nSEND/nTX/nCS` từ registry + relationship map.
4. Runtime observer chỉ xác nhận các flow thật sự active.

### Unit tests bắt buộc

Topology 3-node theo paper:

```text
S1: nSEND=2, nTX=3, nCS=0, ntotal=3, FBRS=2/3
S2: nSEND=1, nTX=3, nCS=0, ntotal=3, FBRS=1/3
```

Thêm test có một CCA-only flow để xác nhận `nCS=1`, không tăng thành 2 khi có
nhiều hơn một CS flow.

### Acceptance gate

Counter tests PASS cho mọi station role và không dùng TCP ACK như flow riêng.

## Step 7 — Tích hợp Algorithm 1 estimator vào Scenario 1 (measure-only)

### Mục tiêu

Reuse implementation hiện có, tạo một estimator per station và chưa thay đổi
CW/cwnd.

### Công việc

1. Tạo `CatraActiveTimeEstimator` riêng cho mỗi Wi-Fi station.
2. Gắn `ObserveMacFrameRx/Tx` vào đúng PHY của station đó.
3. Dùng một `CatraMacTransactionTracker` per station để tránh state xuyên node.
4. Schedule report/reset đúng mỗi `EP=2s` từ cùng một time origin.
5. Join sample `RBRs` với flow-count values từ Step 6.

### Station CSV

```text
time,node,nSEND,nTX,nCS,ntotal,FBRS,
raw_active_s,smoothed_active_s,RBRS,
packet_count,data_count,tcp_ack_count
```

### Invariants

```text
0 <= RBRS <= 1
FBRS = nSEND / ntotal khi nSEND>0 và ntotal>0
raw accumulator reset sau mỗi EP
smoothed TActive giữ history
```

### Acceptance gate

- `baseline` và `measure-only` cho cùng forwarding/TCP behavior trong sai số
  stochastic hợp lý.
- Không có call thay đổi CW/cwnd.
- Per-station state không dùng global singleton.

## Step 8 — Reconcile CW representation và tạo CATRA MAC controller thuần

### Mục tiêu

Chốt semantics CW trước khi nối controller vào Wi-Fi runtime.

### Công việc

1. Từ source mapping, ghi rõ quan hệ giữa paper `CWmin=32/CWmax=1024` và
   ns-3 inclusive CW slot range.
2. Quyết định `CW_original` là current CW, pre-CATRA CW hay base CW tại thời
   điểm backoff; ghi rõ trong README.
3. Viết pure function:

```text
newCW = clamp((RBRS / FBRS) * originalCW, minCW, maxCW)
```

4. Xử lý `nSEND=0`, `ntotal=0`, `FBRS=0`, NaN/inf và rounding.
5. Không nối function này vào `Txop` trước khi unit tests PASS.

### Unit tests

```text
FBR=0.50, RBR=0.25, CW=64 -> 32
FBR=0.25, RBR=0.50, CW=64 -> 128
upper clamp -> CWmax
lower clamp -> CWmin
zero-flow -> no update
```

### Acceptance gate

Pure controller tests và CW representation note đều hoàn tất.

## Step 9 — Tích hợp CATRA MAC

### Mục tiêu

Áp dụng CW mới tại đúng backoff boundary, không phá DCF retry/BEB state.

### Trình tự

1. Tạo mode `catra-mac` dùng cùng topology, routes, traffic và measurement với
   baseline.
2. Hook controller ở điểm đã xác định trong `NS3_SOURCE_MAPPING.md`.
3. Chỉ station có SEND flow mới nhận CATRA CW update.
4. Ghi old/new CW và lý do update trước khi backoff mới được chọn.
5. Giữ baseline path hoàn toàn không gọi hook này.

### Control CSV

```text
time,node,nSEND,nTX,nCS,ntotal,FBRS,RBRS,ratio,
original_cw,old_cw,new_cw,min_cw,max_cw,decision
```

### Validation

- `RBRs < FBRS` dẫn tới CW nhỏ hơn, trừ lower clamp.
- `RBRs > FBRS` dẫn tới CW lớn hơn, trừ upper clamp.
- Backoff sau update dùng CW mới.
- Retry failure/success vẫn thực hiện BEB/reset theo semantics đã mô tả.
- So sánh `baseline` với `catra-mac` bằng cùng seed/run.

### Acceptance gate

Direction tests PASS, runtime trace chứng minh CW thực sự được dùng, và MAC
fairness cải thiện mà không làm mất connectivity.

## Step 10 — Per-flow active time và CATRA TCP inputs

### Mục tiêu

Chuẩn bị đủ input của Algorithm 2 nhưng chưa điều khiển TCP.

### Data model

```cpp
struct CatraFlowState
{
    CatraFlowId id;
    Time activeTime;
    uint64_t packetCount;
    double fbr;
    double rbr;
    Time averageTxTime;
    uint32_t nTotal;
};
```

### Giá trị cần đo

```text
FBRf = 1 / ntotal
RBRf = TActiveFlow / EP
Nf = transmitted packet count in EP
Ttr_f = TActiveFlow / Nf
win = cwnd + highestAck - currentSequence
Tf = ntotal * win * Ttr_f
ratio = RBRf / FBRf
```

### Quy tắc unit

- `cwnd`, highest ACK và current sequence phải cùng đơn vị byte/sequence byte.
- `Ttr_f`, `Tf`, `deltaF` dùng ns-3 `Time` hoặc seconds có conversion rõ.
- `Nf=0` không được chia; sample được đánh dấu inactive/no-decision.

### Flow CSV trước control

```text
time,node,flow,ntotal,FBRf,RBRf,ratio,Nf,Ttr_s,
cwnd_bytes,highest_ack,current_seq,win_bytes,Tf_s
```

### Acceptance gate

Tất cả intermediate values có thể giải thích từ packet/socket trace và không
có control side effect.

## Step 11 — CATRA TCP controller thuần

### Mục tiêu

Chuyển Algorithm 2 thành decision function có thể unit-test.

### Decision contract

```text
ratio > Highth=1.05:
    cwnd_delta = -1 segment, minimum 1 segment
    deltaF = ratio * Tf
    action = DECREASE_AND_DELAY

ratio < Lowth=0.7:
    cwnd_delta = +1 segment
    deltaF = 0
    action = INCREASE

otherwise:
    action = ORIGINAL_TCP
```

### Unit tests

- Boundary ngay dưới/bằng/trên `Lowth`.
- Boundary ngay dưới/bằng/trên `Highth`.
- `cwnd=1 MSS` với decrease không xuống dưới 1.
- `Nf=0`, `FBRf=0`, invalid ratio -> no decision/error rõ.
- `deltaF` đúng unit và không âm.

### Acceptance gate

Decision tests PASS mà chưa cần chạy Wi-Fi scenario.

## Step 12 — Tích hợp CATRA TCP

### Mục tiêu

Tạo mode `catra-full` bằng cách nối decision đã kiểm thử vào socket/application
đúng thời điểm.

### Trình tự

1. Xác định socket nào thuộc Flow 1/Flow 2; không dùng wildcard mơ hồ.
2. Áp `cwnd` delta qua hook đã map, bảo toàn recovery state của TCP.
3. Áp `deltaF` vào packet-generation scheduling, không dùng blocking sleep.
4. Khi action `ORIGINAL_TCP`, giao hoàn toàn cho NewReno path.
5. Trace trước/sau: ratio, action, old/new cwnd, delay và next send time.

### Flow control CSV

```text
time,node,flow,ntotal,FBRf,RBRf,ratio,Nf,Ttr_s,
win_bytes,Tf_s,deltaF_s,old_cwnd_bytes,new_cwnd_bytes,action
```

### Acceptance gate

- `ratio>1.05`: cwnd giảm đúng 1 MSS và delay > 0.
- `ratio<0.7`: cwnd tăng đúng 1 MSS và delay = 0.
- Middle band: original NewReno behavior.
- RTO/Fast Recovery của NewReno không bị CATRA label nhầm thành CATRA action.

## Step 13 — Full comparison và regression matrix

### Matrix tối thiểu

```text
n = 3,4,5,6
mode = baseline, measure-only, catra-mac, catra-full
seed = fixed
run = smoke set
```

Sau smoke matrix, chạy statistical matrix với 10–30 runs/configuration.

### Regression checks

- PHY range probe.
- Topology invariant.
- Route probe.
- Metric formula tests.
- Flow counter tests.
- Algorithm 1 strict probe.
- MAC controller unit/runtime checks.
- TCP controller unit/runtime checks.

### Acceptance gate

- `measure-only` không thay đổi baseline có hệ thống.
- `catra-mac` có CW direction đúng.
- `catra-full` cải thiện fairness so với baseline theo aggregate statistics.
- Không lựa chọn/cherry-pick run riêng để chứng minh kết quả.

## Step 14 — Experiment runner, summary và plotting

### Files đề xuất

```text
scripts/catra/run-scenario1.sh
scripts/catra/summarize-scenario1.py
scripts/catra/plot-scenario1-fig4.py
```

### Runner requirements

- Fail fast khi executable trả nonzero.
- Tạo output directory theo mode/n/seed/run.
- Ghi exact command, git SHA và ns-3 version.
- Không overwrite run đã tồn tại nếu chưa có flag rõ ràng.
- Hỗ trợ smoke duration và full 300 s duration.

### Summary requirements

- Validate schema trước khi aggregate.
- Group theo `mode,n`.
- Xuất sample count, mean, standard deviation và confidence interval.
- Báo missing/duplicate run.

### Plot requirements

- Flow 1 và Flow 2 throughput theo `n`.
- Total E2E và hop-weighted throughput với label không nhập nhằng.
- Jain fairness.
- Error bars khi có nhiều run.
- Ghi rõ `TcpNewReno PORT`, không ghi Tahoe nếu chưa triển khai Tahoe-like.

## Step 15 — Documentation và Definition of Done

### Cập nhật README

Mỗi implementation choice không có trong paper phải có:

```text
Choice
Reason
Expected impact
Validation evidence
Status: PAPER / PORT / CALIBRATE / UNRESOLVED
```

### Definition of Done

- [ ] Phase 0–2 được revalidate trên checkout hiện tại.
- [ ] Static routes đúng cả forward/reverse cho `n=3..6`.
- [ ] Baseline TCP two-flow chạy 300 s và xuất CSV đúng schema.
- [ ] Baseline matrix thể hiện hoặc giải thích được long-hop unfairness.
- [ ] Flow counter đúng `nSEND/nTX/nCS/ntotal`.
- [ ] Algorithm 1 chạy per station trong Scenario 1, read-only mode PASS.
- [ ] CW representation được giải quyết và ghi tài liệu.
- [ ] CATRA MAC controller unit/runtime tests PASS.
- [ ] CATRA TCP intermediate metrics có trace kiểm chứng.
- [ ] CATRA TCP controller unit/runtime tests PASS.
- [ ] Full matrix có nhiều runs và aggregate statistics.
- [ ] Plot cuối dùng đúng metric/label và có error bars.
- [ ] Mọi deviation khỏi paper được phân loại rõ.

## 5. File ownership dự kiến

Chỉ tách file khi responsibility đã ổn định. Thứ tự đề xuất:

```text
scratch/catra/
├── catra-scenario1.cc              # CLI + orchestration
├── scenario1-topology.cc/.h        # nodes, PHY, positions, addresses
├── scenario1-routing.cc/.h         # static routes + route probe
├── scenario1-traffic.cc/.h         # TCP applications and sinks
├── scenario1-metrics.cc/.h         # throughput and CSV
├── flow-counting.cc/.h             # pure flow-count model
├── active-time-estimation/         # existing Algorithm 1 components
├── catra-mac-controller.cc/.h      # pure decision + runtime adapter
├── catra-tcp-controller.cc/.h      # pure decision + runtime adapter
├── NS3_SOURCE_MAPPING.md
├── README.md
└── SCENARIO1_EXECUTION_PLAN.md
```

Chưa chuyển sang `contrib/catra` cho đến khi baseline, measurement contracts và
controller interfaces ổn định. Khi chuyển, giữ executable assembly trong
`scratch/catra` và chỉ chuyển reusable model/controller code.

## 6. Kế hoạch commit

Mỗi commit phải build/test được độc lập:

```text
docs: add CATRA Scenario 1 source mapping
feat: add deterministic Scenario 1 static routes
test: validate Scenario 1 forward and reverse paths
feat: add Scenario 1 Original TCP baseline
feat: export Scenario 1 throughput metrics
test: add deterministic Scenario 1 metric checks
feat: add CATRA flow-count model
test: validate CATRA station flow counts
feat: integrate Algorithm 1 measurements into Scenario 1
feat: add CATRA MAC decision model
test: validate CATRA MAC CW decisions
feat: apply CATRA MAC control at backoff boundary
feat: add CATRA per-flow TCP measurements
feat: add CATRA TCP decision model
test: validate CATRA TCP decisions
feat: integrate CATRA full mode
feat: add Scenario 1 experiment runner and plots
docs: record Scenario 1 validation evidence
```

Không gộp source mapping, baseline, MAC control và TCP control vào cùng một
commit.

## 7. Rủi ro và điểm dừng bắt buộc

| Rủi ro | Cách xử lý | Điểm dừng |
|---|---|---|
| Baseline không có unfairness | Kiểm tra route, PHY, queue, RTS/CTS, saturation | Không triển khai controller |
| CW paper/ns-3 lệch semantics | Source mapping + deterministic backoff test | Không gọi `SetMinCw/SetMaxCw` |
| Algorithm 1 double-count MAC ACK | Giữ transaction tracker + strict probe | Không dùng RBRs cho control |
| `RBRs > 1` | Audit transaction overlap/accounting/EP boundary | Không chạy CATRA MAC |
| TCP units không đồng nhất | Log byte/segment/sequence conversion | Không chạy Algorithm 2 |
| CATRA hook phá recovery | So sánh socket state/retransmission với baseline | Quay lại measure-only |
| Kết quả phụ thuộc một seed | Chạy nhiều `RngRun`, aggregate statistics | Không kết luận từ single run |
| Log/PCAP quá lớn | Trace flags, smoke run, PCAP off mặc định | Không bật toàn bộ cho 300 s |

## 8. Bước thực hiện tiếp theo

Bước code tiếp theo là **Step 0 revalidation**, sau đó **Step 1 source mapping**
và **Step 2 static routing/UDP route probe**. Không bắt đầu baseline TCP trước
khi forward và reverse paths đã được chứng minh cho đủ `n=3..6`.

## 9. Execution record — n=3 first

- [ ] Step 0 runtime revalidation: blocked because Docker daemon is unavailable.
- [x] Step 1 routing/Algorithm 1 source mapping recorded.
- [ ] Step 1 MAC/TCP control-hook mapping; intentionally deferred until their phases.
- [x] Step 2 route-probe implementation added behind `--mode=route-probe`.
- [x] Forward `/32` routes toward `R` implemented.
- [x] Reverse `/32` routes toward `S2` implemented.
- [x] Long-forward, long-reverse and short-forward UDP probes implemented.
- [x] FlowMonitor delivery/hop-count validator implemented.
- [ ] Build and strict runtime validation for `n=3`.
- [ ] Original TCP baseline; intentionally gated on route-probe PASS.

Only `n=3` is in the current acceptance scope. The implementation remains
parameterized by `--n` so later validation can extend to `n=4..6` without
rewriting routing logic.
