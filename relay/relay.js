// Hollow Pines relay - lets a PS2 (plain TCP) play with phones/PCs (WebSocket) in the same lobby.
// No dependencies: only Node built-ins. Run:  node relay.js   (or double-click the .exe build)
//
//  - HTTP  :8080  serves hollow_pines.html  (open http://<this PC's IP>:8080 on your phone/PC)
//  - WS    :8080  /ws       browsers (host or joiner)
//  - TCP   :7777            the PS2: send  "JOIN ABCDE\n"  then one JSON message per line
//
// Browser host = the authority (same as the old PeerJS version). The relay only forwards messages.
'use strict';
const http = require('http'), net = require('net'), crypto = require('crypto'), fs = require('fs'), path = require('path'), os = require('os');

const HTTP_PORT = +process.env.PORT || 8080, TCP_PORT = +process.env.TCP_PORT || 7777;
const CODE_AL = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
const rooms = new Map();                       // code -> {host, peers:Map(pid->peer), next}
const log = (...a) => console.log(new Date().toLocaleTimeString(), ...a);
const randCode = () => { let s = ''; for (let i = 0; i < 5; i++) s += CODE_AL[crypto.randomInt(CODE_AL.length)]; return s; };

// ---------------------------------------------------------------- tiny WebSocket (text frames only)
function wsAccept(key) { return crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64'); }
function wsFrame(str) {
  const p = Buffer.from(str), n = p.length;
  const h = n < 126 ? Buffer.from([0x81, n]) : n < 65536 ? Buffer.from([0x81, 126, n >> 8, n & 255]) : (() => { const b = Buffer.alloc(10); b[0] = 0x81; b[1] = 127; b.writeBigUInt64BE(BigInt(n), 2); return b; })();
  return Buffer.concat([h, p]);
}
function wsParser(onText, onClose) {            // incremental parser for client->server (masked) frames
  let buf = Buffer.alloc(0);
  return chunk => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      if (buf.length < 2) return;
      const op = buf[0] & 15; let len = buf[1] & 127, off = 2;
      if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
      const masked = !!(buf[1] & 128); if (masked) off += 4;
      if (len > 1 << 20) return onClose();      // refuse silly sizes
      if (buf.length < off + len) return;
      let data = buf.subarray(off, off + len);
      if (masked) { const k = buf.subarray(off - 4, off); data = Buffer.from(data); for (let i = 0; i < data.length; i++) data[i] ^= k[i & 3]; }
      buf = buf.subarray(off + len);
      if (op === 1) onText(data.toString('utf8')); else if (op === 8) return onClose();
      else if (op === 9) { /* ping: ignore, browsers don't need pong from us */ }
    }
  };
}

// ---------------------------------------------------------------- peers (either kind)
// peer = {kind:'ws'|'tcp', send(obj), close(), room, pid, isHost}
function sendHostEnvelope(room, obj) { if (room.host) room.host.send(obj); }
function dropPeer(p) {
  const room = p.room; if (!room || p.dead) return; p.dead = true;
  if (p.isHost) {
    log('room', room.code, 'closed (host left)');
    for (const q of room.peers.values()) { q.send({ r: 'err', why: 'The host closed the game.' }); q.close(); }
    rooms.delete(room.code);
  } else {
    room.peers.delete(p.pid);
    sendHostEnvelope(room, { r: 'gone', pid: p.pid });
    log('room', room.code, 'player', p.pid, 'left (' + p.kind + ')');
  }
}
function hostRoom(p, wanted) {
  let code = (wanted || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
  if (code.length !== 5 || rooms.has(code)) { do code = randCode(); while (rooms.has(code)); }
  const room = { code, host: p, peers: new Map(), next: 1 };
  rooms.set(code, room); p.room = room; p.isHost = true; p.pid = 0;
  p.send({ r: 'hosted', code }); log('room', code, 'opened');
}
function joinRoom(p, code) {
  const room = rooms.get((code || '').toUpperCase());
  if (!room) { p.send({ r: 'err', why: 'No game found with that code.' }); p.close(); return; }
  if (room.peers.size >= 3) { p.send({ r: 'err', why: 'This game is full (4 players).' }); p.close(); return; }
  const pid = room.next++; p.room = room; p.pid = pid; room.peers.set(pid, p);
  p.send({ r: 'joined' }); sendHostEnvelope(room, { r: 'peer', pid });
  log('room', room.code, 'player', pid, 'joined via', p.kind);
}
// message from a peer -> where it should go
function fromPeer(p, msg) {
  const room = p.room; if (!room) return;
  if (p.isHost) {                                 // host -> {r:'d', to:pid|'*', m}
    if (msg.r !== 'd' || typeof msg.m !== 'object') return;
    if (msg.to === '*' || msg.to == null) { for (const q of room.peers.values()) q.send({ r: 'd', m: msg.m }); }
    else { const q = room.peers.get(+msg.to); if (q) q.send({ r: 'd', m: msg.m }); }
  } else {                                        // joiner -> host (+ the host re-broadcasts what it wants)
    if (msg.r !== 'd' || typeof msg.m !== 'object') return;
    sendHostEnvelope(room, { r: 'd', pid: p.pid, m: msg.m });
  }
}

// ---------------------------------------------------------------- HTTP + WebSocket (browsers)
const here = process.pkg ? path.dirname(process.execPath) : __dirname;
function findGame() { for (const n of ['hollow_pines.html', 'index.html']) { const f = path.join(here, n); if (fs.existsSync(f)) return f; } return null; }
const server = http.createServer((req, res) => {
  if (req.url === '/' || req.url.startsWith('/?') || req.url === '/hollow_pines.html') {
    const f = findGame();
    if (!f) { res.writeHead(404, { 'content-type': 'text/plain' }); return res.end('Put hollow_pines.html next to the relay and refresh.'); }
    let html = fs.readFileSync(f, 'utf8');                       // tell the page it is running through the relay
    html = html.replace('<head>', '<head><script>window.HP_RELAY=true</script>');
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' }); return res.end(html);
  }
  res.writeHead(404); res.end();
});
server.on('upgrade', (req, sock) => {
  const key = req.headers['sec-websocket-key'];
  if (!key || (req.url || '').split('?')[0] !== '/ws') { sock.destroy(); return; }
  sock.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + wsAccept(key) + '\r\n\r\n');
  sock.setNoDelay(true);
  const p = { kind: 'ws', room: null, dead: false, send: o => { try { if (!sock.destroyed) sock.write(wsFrame(JSON.stringify(o))); } catch (e) {} }, close: () => { try { sock.end(); } catch (e) {} } };
  const onClose = () => { dropPeer(p); try { sock.destroy(); } catch (e) {} };
  sock.on('data', wsParser(text => {
    let m; try { m = JSON.parse(text); } catch (e) { return; }
    if (!p.room) { if (m.r === 'host') hostRoom(p, m.code); else if (m.r === 'join') joinRoom(p, m.code); return; }
    fromPeer(p, m);
  }, onClose));
  sock.on('close', onClose); sock.on('error', onClose);
});

// ---------------------------------------------------------------- TCP (the PS2): "JOIN CODE\n" then JSON lines both ways
const tcp = net.createServer(sock => {
  log('PS2 connection arrived from', sock.remoteAddress);
  sock.setNoDelay(true); sock.setEncoding('utf8');
  const p = { kind: 'tcp', room: null, dead: false,
    // PS2 gets the bare game message as one line; envelope bits it can't use are dropped
    send: o => { try { if (o.r === 'd') sock.write(JSON.stringify(o.m) + '\n'); else if (o.r === 'err') sock.write(JSON.stringify({ t: 'deny', why: o.why }) + '\n'); else if (o.r === 'joined') sock.write('{"t":"relay-ok"}\n'); } catch (e) {} },
    close: () => { try { sock.end(); } catch (e) {} } };
  let buf = '';
  const onClose = () => dropPeer(p);
  sock.on('data', d => {
    buf += d; if (buf.length > 1 << 16) { sock.destroy(); return; }
    let i; while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1); if (!line) continue;
      if (!p.room) { const mm = /^JOIN\s+([A-Za-z0-9]{5})$/.exec(line); if (mm) joinRoom(p, mm[1]); else { p.send({ r: 'err', why: 'Send JOIN <code> first.' }); p.close(); } continue; }
      let m; try { m = JSON.parse(line); } catch (e) { continue; }
      fromPeer(p, { r: 'd', m });
    }
  });
  sock.on('close', onClose); sock.on('error', onClose);
});

function lanIPs() { const o = []; for (const l of Object.values(os.networkInterfaces())) for (const a of l || []) if (a.family === 'IPv4' && !a.internal) o.push(a.address); return o; }
server.listen(HTTP_PORT, '0.0.0.0', () => {
  tcp.listen(TCP_PORT, '0.0.0.0', () => {
    const ips = lanIPs();
    console.log('\n  HOLLOW PINES RELAY is running\n');
    for (const ip of ips) console.log('   Phone/PC browser :  http://' + ip + ':' + HTTP_PORT);
    for (const ip of ips) console.log('   PS2 server IP    :  ' + ip + '   (port ' + TCP_PORT + ')');
    if (!findGame()) console.log('\n   (!) hollow_pines.html not found next to the relay - put it in the same folder.');
    console.log('\n  Leave this window open while you play.\n');
  });
});
server.on('error', e => { console.error('Could not start:', e.message); process.exit(1); });
