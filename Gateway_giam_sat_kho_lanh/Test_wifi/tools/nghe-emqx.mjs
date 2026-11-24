// NGHE EMQX THAT de xem gateway WiFi gui gi len (CHI DOC, khong ghi gi vao app/Supabase).
// Dung lai thu vien + cau hinh broker cua web app (cau-noi/.env, goi mqtt trong cau-noi/node_modules).
//
//   node tools/nghe-emqx.mjs                 nghe kholanh/# , giai ma bang bo giai ma cua app
//   node tools/nghe-emqx.mjs --lenh ping     gui 1 lenh xuong gateway (ping|status|sim_on|sim_off|reset_wifi)
//   node tools/nghe-emqx.mjs --gateway UG65-0FA31C --app "E:/App-giám-sat-kho-lạnh"
import { createRequire } from "node:module";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const ts = process.argv.slice(2);
const lay = (k, md) => { const i = ts.indexOf(k); return i >= 0 ? ts[i + 1] : md; };
const APP = lay("--app", "E:/App-giám-sat-kho-lạnh");
const GW = lay("--gateway", "UG65-0FA31C").toLowerCase();
const LENH = lay("--lenh");
const gio = () => new Date().toLocaleTimeString("vi-VN");

const CAU_NOI = join(APP, "cau-noi");
const mqtt = createRequire(join(CAU_NOI, "package.json"))("mqtt");
const nap = (f) => import(pathToFileURL(join(CAU_NOI, "lib", f)).href);
const { cauHinhMqtt } = await nap("ket-noi.mjs");
const { chuanHoaUplink } = await nap("giai-ma-milesight.mjs");

const { url, options } = cauHinhMqtt(`nghe-gw-wifi-${Math.random().toString(16).slice(2, 8)}`);
const client = mqtt.connect(url, options);
client.on("error", (e) => console.error(`[${gio()}] loi MQTT: ${e.message}`));
client.on("connect", () => {
  console.log(`[${gio()}] Da noi ${url}. Dang nghe kholanh/# (Ctrl+C de dung)\n`);
  client.subscribe("kholanh/#", { qos: 1 });
  if (LENH) {
    client.publish(`kholanh/gw/${GW}/cmd`, LENH, { qos: 1 });
    console.log(`[${gio()}] -> gui lenh "${LENH}" toi kholanh/gw/${GW}/cmd`);
  }
});

const dem = new Map();
client.on("message", (topic, buf) => {
  const s = buf.toString();
  if (topic.includes("/gw/")) {
    console.log(`[${gio()}] ${topic}  ${s}`);
    return;
  }
  try {
    const u = chuanHoaUplink(JSON.parse(s));
    dem.set(u.devEUI, (dem.get(u.devEUI) || 0) + 1);
    const gt = u.trangThaiCua ? `cua ${u.trangThaiCua}` : `${u.nhietDo}°C ${u.doAm}%`;
    console.log(`[${gio()}] ${u.devEUI.padEnd(11)} ${gt.padEnd(14)} pin ${u.pin}%  fCnt ${u.fCnt}  do luc ${u.thoiGian ?? "?"}  gw ${u.gatewayId}  (#${dem.get(u.devEUI)})`);
  } catch (e) {
    console.log(`[${gio()}] ${topic}  KHONG GIAI MA DUOC (${e.message}): ${s.slice(0, 160)}`);
  }
});
