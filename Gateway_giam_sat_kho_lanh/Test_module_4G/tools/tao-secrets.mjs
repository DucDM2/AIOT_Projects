// Tao include/secrets.h tu file .env cua cau noi web app (MQTT_URL / MQTT_USER / MQTT_PASS)
// Chay:  node tools/tao-secrets.mjs ["E:/App-giám-sat-kho-lạnh/cau-noi/.env"]
// KHONG in mat khau ra man hinh. secrets.h da nam trong .gitignore.
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const GOC = join(dirname(fileURLToPath(import.meta.url)), "..");
const nguon = process.argv[2] || "E:/App-giám-sat-kho-lạnh/cau-noi/.env";
const env = {};
for (const dong of readFileSync(nguon, "utf8").split(/\r?\n/)) {
  const s = dong.trim();
  if (!s || s.startsWith("#") || !s.includes("=")) continue;
  const i = s.indexOf("=");
  let v = s.slice(i + 1).trim();
  if (/^(["']).*\1$/.test(v)) v = v.slice(1, -1);
  env[s.slice(0, i).trim()] = v;
}
const loi = (m) => { console.error("✗ " + m); process.exit(1); };
if (!env.MQTT_URL) loi(`Khong thay MQTT_URL trong ${nguon}`);
const u = new URL(env.MQTT_URL);
const tls = u.protocol === "mqtts:" || u.protocol === "ssl:";
const port = Number(u.port || (tls ? 8883 : 1883));
const user = env.MQTT_USER || "";
const pass = env.MQTT_PASS || "";
// A7680C dat user/pass trong "..." cua lenh AT -> khong duoc chua dau " hoac ky tu dieu khien
for (const [ten, v] of [["MQTT_USER", user], ["MQTT_PASS", pass], ["host", u.hostname]])
  if (/["\x00-\x1f\x7f]/.test(v)) loi(`${ten} chua dau " hoac ky tu dieu khien - A7680C khong gui duoc. Doi mat khau tren EMQX.`);
const c = (v) => JSON.stringify(v); // chuoi C hop le (escape \ )
const noiDung = `// TU DONG TAO boi tools/tao-secrets.mjs tu ${nguon.split(String.fromCharCode(92)).join("/")}
// KHONG dua file nay len git.
#pragma once
#define SECRET_MQTT_HOST ${c(u.hostname)}
#define SECRET_MQTT_PORT ${port}
#define SECRET_MQTT_TLS ${tls ? 1 : 0}
#define SECRET_MQTT_USER ${c(user)}
#define SECRET_MQTT_PASS ${c(pass)}
#define SECRET_MQTT_CA_PEM ""
`;
writeFileSync(join(GOC, "include", "secrets.h"), noiDung);
console.log(`✓ Da tao include/secrets.h: broker ${u.hostname}:${port} (${tls ? "TLS" : "TCP"}), user ${user ? "co" : "khong"}, mat khau ${pass ? "co" : "khong"}`);
