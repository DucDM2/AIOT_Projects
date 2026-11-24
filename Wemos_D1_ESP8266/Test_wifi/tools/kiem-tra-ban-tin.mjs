// Kiem tra ban tin firmware tao ra (lay tu mo phong: test/sim/build/uplinks.tsv) bang CHINH
// bo giai ma cua web app (cau-noi/lib/giai-ma-milesight.mjs) -> dam bao app doc dung 100%.
// Chay: node tools/kiem-tra-ban-tin.mjs [duong-dan-app]
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const GOC = join(dirname(fileURLToPath(import.meta.url)), "..");
const APP = process.argv[2] || "E:/App-giám-sat-kho-lạnh";
const { chuanHoaUplink, giaiMaTLV } = await import(pathToFileURL(join(APP, "cau-noi/lib/giai-ma-milesight.mjs")).href);
const { DS_THIET_BI } = await import(pathToFileURL(join(APP, "cau-noi/lib/mo-phong.mjs")).href);

const dong = readFileSync(join(GOC, "test/sim/build/uplinks.tsv"), "utf8").split("\n").filter(Boolean);
let ok = 0, hong = 0, status = 0;
const loi = (m) => { hong++; if (hong <= 20) console.log("  ✗ " + m); };
const thietBi = new Map(DS_THIET_BI.map((t) => [t.id, t]));
for (const d of dong) {
  const [topic, json] = d.split("\t");
  if (topic.includes("/gw/")) { JSON.parse(json); status++; continue; }
  let msg;
  try { msg = JSON.parse(json); } catch (e) { loi(`JSON hong: ${json.slice(0, 80)}`); continue; }
  const u = chuanHoaUplink(msg);
  const tb = thietBi.get(u.devEUI);
  const m = topic.match(/^kholanh\/([^/]+)\/up$/);
  if (!m || m[1].toUpperCase() !== u.devEUI) { loi(`topic ${topic} khong khop devEUI ${u.devEUI}`); continue; }
  if (!tb) { loi(`${u.devEUI} khong co trong danh muc thiet bi cua app`); continue; }
  if (u.gatewayId !== tb.gateway_id) { loi(`${u.devEUI}: gateway ${u.gatewayId} != ${tb.gateway_id}`); continue; }
  if (!u.thoiGian || Number.isNaN(Date.parse(u.thoiGian))) { loi(`${u.devEUI}: thieu/sai time`); continue; }
  if (u.fCnt == null || u.pin !== tb.pin || u.rssi == null || u.snr == null || u.fPort == null) { loi(`${u.devEUI}: thieu truong ${JSON.stringify(u)}`); continue; }
  const tlv = giaiMaTLV(Buffer.from(msg.data, "base64").toString("hex"));
  if (tb.loai === "cua") {
    if (!["mo", "dong"].includes(u.trangThaiCua) || u.nhietDo !== null) { loi(`${u.devEUI}: cua sai ${JSON.stringify(u)}`); continue; }
    if (tlv.magnet_status !== msg.object.magnet_status) { loi(`${u.devEUI}: TLV cua khac object`); continue; }
  } else {
    if (typeof u.nhietDo !== "number" || typeof u.doAm !== "number" || u.trangThaiCua !== null) { loi(`${u.devEUI}: nhiet/am sai ${JSON.stringify(u)}`); continue; }
    if (Math.abs(tlv.temperature - u.nhietDo) > 0.05 || Math.abs(tlv.humidity - u.doAm) > 0.5 || tlv.battery !== u.pin) { loi(`${u.devEUI}: TLV ${JSON.stringify(tlv)} khac object`); continue; }
    if (u.nhietDo < -40 || u.nhietDo > 60 || u.doAm < 0 || u.doAm > 100) { loi(`${u.devEUI}: gia tri vo ly`); continue; }
  }
  ok++;
}
console.log(`\nBan tin cam bien: ${ok} dung, ${hong} sai | ban tin status gateway: ${status} (JSON hop le)`);
const vd = dong.find((d) => d.includes("/up\t"));
if (vd) {
  const [t, j] = vd.split("\t");
  console.log(`\nVi du ban tin gui len ${t}:\n${j}\n\n-> app giai ma thanh:\n${JSON.stringify(chuanHoaUplink(JSON.parse(j)), null, 1)}`);
}
process.exitCode = hong || !ok ? 1 : 0;
