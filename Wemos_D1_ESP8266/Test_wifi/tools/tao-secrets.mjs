// Tao include/secrets.h tu:
//   - file .env cua cau noi web app (MQTT_URL / MQTT_USER / MQTT_PASS)  -> broker EMQX
//   - file wifi.env o thu muc goc Test_wifi (WIFI_SSID / WIFI_PASS ...) -> mang WiFi
// Chay:  node tools/tao-secrets.mjs ["E:/App-giám-sat-kho-lạnh/cau-noi/.env"]
// KHONG in mat khau ra man hinh. secrets.h va wifi.env da nam trong .gitignore.
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const GOC = join(dirname(fileURLToPath(import.meta.url)), "..");
const nguon = process.argv[2] || "E:/App-giám-sat-kho-lạnh/cau-noi/.env";
const fileWifi = join(GOC, "wifi.env");
const loi = (m) => { console.error("✗ " + m); process.exit(1); };

function docEnv(p) {
  const env = {};
  for (const dong of readFileSync(p, "utf8").split(/\r?\n/)) {
    const s = dong.trim();
    if (!s || s.startsWith("#") || !s.includes("=")) continue;
    const i = s.indexOf("=");
    let v = s.slice(i + 1).trim();
    if (/^(["']).*\1$/.test(v)) v = v.slice(1, -1);
    env[s.slice(0, i).trim()] = v;
  }
  return env;
}

if (!existsSync(nguon)) loi(`Khong thay ${nguon}`);
const env = docEnv(nguon);
if (!env.MQTT_URL) loi(`Khong thay MQTT_URL trong ${nguon}`);
const u = new URL(env.MQTT_URL);
const tls = u.protocol === "mqtts:" || u.protocol === "ssl:";
const port = Number(u.port || (tls ? 8883 : 1883));
const user = env.MQTT_USER || "";
const pass = env.MQTT_PASS || "";

if (!existsSync(fileWifi))
  loi(`Chua co ${fileWifi}. Sao chep wifi.env.example thanh wifi.env roi dien WIFI_SSID / WIFI_PASS.`);
const w = docEnv(fileWifi);
const ssid = w.WIFI_SSID || "", wpass = w.WIFI_PASS || "";
const ssid2 = w.WIFI_SSID_2 || "", wpass2 = w.WIFI_PASS_2 || "";
if (!ssid || ssid === "TenWifi") loi("wifi.env: chua dien WIFI_SSID");
for (const [ten, v, max] of [["WIFI_SSID", ssid, 32], ["WIFI_PASS", wpass, 64], ["WIFI_SSID_2", ssid2, 32], ["WIFI_PASS_2", wpass2, 64]]) {
  if (Buffer.byteLength(v) > max) loi(`${ten} dai qua ${max} byte`);
  if (/[\x00-\x1f\x7f]/.test(v)) loi(`${ten} chua ky tu dieu khien`);
}
if (wpass && wpass.length < 8) loi("WIFI_PASS WPA2 phai >= 8 ky tu (de trong neu la mang mo)");

const c = (v) => JSON.stringify(v); // chuoi C hop le (escape \ va ")
const noiDung = `// TU DONG TAO boi tools/tao-secrets.mjs tu ${nguon.split(String.fromCharCode(92)).join("/")} + wifi.env
// KHONG dua file nay len git.
#pragma once
#define SECRET_WIFI_SSID ${c(ssid)}
#define SECRET_WIFI_PASS ${c(wpass)}
#define SECRET_WIFI_SSID2 ${c(ssid2)}
#define SECRET_WIFI_PASS2 ${c(wpass2)}
#define SECRET_MQTT_HOST ${c(u.hostname)}
#define SECRET_MQTT_PORT ${port}
#define SECRET_MQTT_TLS ${tls ? 1 : 0}
#define SECRET_MQTT_USER ${c(user)}
#define SECRET_MQTT_PASS ${c(pass)}
#define SECRET_MQTT_CA_PEM ""
`;
writeFileSync(join(GOC, "include", "secrets.h"), noiDung);
console.log(`✓ Da tao include/secrets.h: WiFi "${ssid}"${ssid2 ? ` (+ du phong "${ssid2}")` : ""}, broker ${u.hostname}:${port} (${tls ? "TLS" : "TCP"}), user ${user ? "co" : "khong"}, mat khau ${pass ? "co" : "khong"}`);
