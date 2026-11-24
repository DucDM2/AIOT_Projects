// GUI DU LIEU GIA LAP TU PC -> ESP32 (USB) -> A7680C 4G -> EMQX -> web app
// Dung CHUNG nguon mo phong voi web app (cau-noi/lib/mo-phong.mjs + ban-tin-gateway.mjs),
// nen so lieu giong het gateway-gia.mjs, chi khac la di qua duong 4G that.
//
// Cach chay (tu thu muc nay):
//   npm install                       (lan dau)
//   node gui.mjs --cong COM5          gui moi cam bien ngay, roi 5 phut/lan
//   node gui.mjs --cong COM5 --mot-lan
//   node gui.mjs --cong COM5 --chu-ky 1 --ep A01=12
//   node gui.mjs --liet-ke            xem cac cong COM
// Tuy chon: --app "E:/App-giám-sat-kho-lạnh"  (mac dinh), --gateway UG65-0FA31C (chi gui cam bien cua gateway nay)
import { SerialPort } from "serialport";
import { ReadlineParser } from "@serialport/parser-readline";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const ts = process.argv.slice(2);
const lay = (k, md) => { const i = ts.indexOf(k); return i >= 0 ? ts[i + 1] : md; };
const log = (...a) => console.log(`[${new Date().toLocaleTimeString("vi-VN")}]`, ...a);

if (ts.includes("--liet-ke")) {
  for (const p of await SerialPort.list()) console.log(`${p.path}\t${p.manufacturer ?? ""}`);
  process.exit(0);
}
const CONG = lay("--cong");
if (!CONG) { console.error("Thieu --cong COMx  (xem: node gui.mjs --liet-ke)"); process.exit(1); }
const APP = lay("--app", "E:/App-giám-sat-kho-lạnh");
const GW = lay("--gateway", "UG65-0FA31C");
const CHU_KY = Number(lay("--chu-ky", "5"));
const MOT_LAN = ts.includes("--mot-lan");
const ep = new Map();
ts.forEach((t, i) => { if (t === "--ep") { const [a, b] = String(ts[i + 1]).split("="); ep.set(a.toUpperCase(), Number(b)); } });

const nap = (f) => import(pathToFileURL(join(APP, "cau-noi/lib", f)).href);
const { DS_THIET_BI, NODE_NGUNG } = await nap("mo-phong.mjs");
const { banTinMilesight } = await nap("ban-tin-gateway.mjs");
const DS = DS_THIET_BI.filter((t) => !NODE_NGUNG.has(t.id) && t.gateway_id === GW);
if (!DS.length) { console.error(`Khong co cam bien nao thuoc gateway ${GW}`); process.exit(1); }

const port = new SerialPort({ path: CONG, baudRate: 115200 });
const parser = port.pipe(new ReadlineParser({ delimiter: "\n" }));
let choTraLoi = null;
parser.on("data", (d) => {
  const s = d.trim();
  if (s.startsWith("OK UP") || s.startsWith("ERR UP")) { choTraLoi?.(s); return; }
  if (s) console.log("   ESP32 " + s);
});
await new Promise((ok, loi) => port.once("open", ok).once("error", loi));
log(`Da mo ${CONG}. Tat gia lap tren ESP32 de khong trung du lieu...`);
await new Promise((r) => setTimeout(r, 2500));  // ESP32 co the vua reset khi mo cong
port.write("sim off\n");

// Mot ban tin moi lan, cho ESP32 xac nhan (ESP32 co the dang ban giao tiep modem toi ~60 s)
async function gui(tb) {
  const fc = (gui.fCnt[tb.id] = (gui.fCnt[tb.id] ?? Math.floor(Math.random() * 50000)) + 1);
  const ma = tb.id.split("-").pop();
  const { msg, mongDoi: m } = banTinMilesight(tb, Date.now(), { dinhDang: "base64", fCnt: fc, epNhiet: ep.get(ma) ?? null });
  const dongLenh = "UP " + JSON.stringify(msg) + "\n";
  for (let lan = 1; lan <= 3; lan++) {
    const kq = await new Promise((ok) => {
      const t = setTimeout(() => ok("HET GIO"), 120000);
      choTraLoi = (s) => { clearTimeout(t); ok(s); };
      port.write(dongLenh);
    });
    choTraLoi = null;
    if (kq.startsWith("OK")) {
      log(`-> ${tb.id}  ${m.trangThaiCua ? "cua " + m.trangThaiCua : `${m.nhietDo}°C ${m.doAm}%`}  (ESP32 da nhan, se gui qua 4G)`);
      return;
    }
    log(`! ${tb.id}: ${kq} (lan ${lan})`);
  }
}
gui.fCnt = {};

const vong = async () => { for (const tb of DS) await gui(tb); };
await vong();
if (MOT_LAN) {
  log("Xong. Doi 60 s de xem ESP32 gui len broker...");
  setTimeout(() => process.exit(0), 60000);
} else {
  log(`Gui lai moi ${CHU_KY} phut. Ctrl+C de dung (ESP32 van giu "sim off"; go "sim on" de bat lai gia lap tren ESP32).`);
  setInterval(() => vong().catch((e) => log("Loi:", e.message)), CHU_KY * 60000);
}
