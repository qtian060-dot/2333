/**
 * OneNET 网页控制台后端(零第三方依赖, Node.js >= 14)
 *
 * 功能:
 *   GET  /            -> 返回 index.html 控制台页面
 *   GET  /api/props   -> 查询设备物模型属性 + 在线状态(代理 OneNET 开放 API, 解决浏览器跨域)
 *   POST /api/props   -> 下发物模型属性(远程开关报警/清零/改阈值)
 *
 * 启动: node server.js   (默认 http://127.0.0.1:8080)
 * 配置: 见下方 ONENET_CONFIG 或用环境变量 ONENET_API_KEY / ONENET_PRODUCT_ID / ONENET_DEVICE_NAME
 */
'use strict';

const http = require('http');
const https = require('https');
const fs = require('fs');
const path = require('path');

/* ================= 配置(必填) ================= */
const ONENET_CONFIG = {
    /* OneNET 开放 API 地址(API 2.0), 可用环境变量 ONENET_API_BASE 覆盖 */
    apiBase: process.env.ONENET_API_BASE || 'https://iot-api.heclouds.com',
    /* 控制台 -> 账户 -> 开放平台 -> 账号Token/API-KEY */
    apiKey: process.env.ONENET_API_KEY || '在此填写你的OneNET开放API-KEY',
    /* 产品ID(与控制台产品详情页一致) */
    productID: process.env.ONENET_PRODUCT_ID || '在此填写产品ID',
    /* 设备名(与控制台设备名一致) */
    deviceName: process.env.ONENET_DEVICE_NAME || '在此填写设备名',
    /* 查询的属性列表(与固件物模型一致) */
    properties: ['count', 'alarm_value', 'alarm_state', 'alarm_enable', 'led_state'],
    /* 本地监听端口 */
    port: 8080
};
/* ============================================== */

function log(msg) {
    console.log('[' + new Date().toLocaleTimeString() + '] ' + msg);
}

/**
 * 检查配置是否已填写
 */
function configError() {
    const c = ONENET_CONFIG;
    if (!c.apiKey || c.apiKey.indexOf('在此填写') >= 0 || /[^\x20-\x7e]/.test(c.apiKey)) {
        return '尚未配置 OneNET API-KEY(在 server.js 顶部 ONENET_CONFIG.apiKey 填写)';
    }
    if (!c.productID || c.productID.indexOf('在此填写') >= 0 || /[^\x20-\x7e]/.test(c.productID)) {
        return '尚未配置产品ID(在 server.js 顶部 ONENET_CONFIG.productID 填写)';
    }
    if (!c.deviceName || c.deviceName.indexOf('在此填写') >= 0 || /[^\x20-\x7e]/.test(c.deviceName)) {
        return '尚未配置设备名(在 server.js 顶部 ONENET_CONFIG.deviceName 填写)';
    }
    return null;
}

/**
 * 调用 OneNET 开放 API
 * @returns {Promise<any>} 解析后的 JSON(或带 error 字段的对象)
 */
function onenetRequest(method, apiPath, body) {
    return new Promise(function (resolve) {
        const bad = configError();
        if (bad) {
            log('配置错误: ' + bad);
            resolve({ error: bad });
            return;
        }
        const url = new URL(ONENET_CONFIG.apiBase + apiPath);
        const data = body ? JSON.stringify(body) : null;
        const isHttps = url.protocol === 'https:';
        const transport = isHttps ? https : http;

        const req = transport.request({
            hostname: url.hostname,
            port: url.port || (isHttps ? 443 : 80),
            path: url.pathname + url.search,
            method: method,
            headers: {
                'api-key': ONENET_CONFIG.apiKey,
                'Content-Type': 'application/json'
            }
        }, function (res) {
            let chunks = '';
            res.on('data', function (c) { chunks += c; });
            res.on('end', function () {
                log(method + ' ' + url.pathname + ' -> HTTP ' + res.statusCode);
                if (res.statusCode !== 200) {
                    resolve({ error: 'HTTP ' + res.statusCode, upstream: chunks.slice(0, 300) });
                    return;
                }
                try {
                    resolve(JSON.parse(chunks));
                } catch (e) {
                    resolve({ error: 'bad JSON', upstream: chunks.slice(0, 300) });
                }
            });
        });
        req.on('error', function (e) {
            resolve({ error: 'request failed: ' + e.message });
        });
        req.setTimeout(8000, function () {
            req.destroy();
            resolve({ error: 'timeout' });
        });
        if (data) req.write(data);
        req.end();
    });
}

/** 组装查询结果 */
async function getProps() {
    const q = '?device_name=' + encodeURIComponent(ONENET_CONFIG.deviceName) +
        '&properties=' + ONENET_CONFIG.properties.join(',');

    const propRes = await onenetRequest('GET',
        '/thingmodel/properties/' + encodeURIComponent(ONENET_CONFIG.productID) + q);
    const stRes = await onenetRequest('GET',
        '/devices/status?device_name=' + encodeURIComponent(ONENET_CONFIG.deviceName));

    const out = { online: false, lastTime: 0, properties: {} };

    if (stRes && stRes.errCode === 0 && stRes.data) {
        out.online = !!stRes.data.online;
        out.lastTime = stRes.data.time || 0;
    }

    if (propRes && propRes.errCode === 0 && Array.isArray(propRes.data) && propRes.data.length) {
        const p = propRes.data[0].properties || {};
        ONENET_CONFIG.properties.forEach(function (k) {
            /* 兼容两种返回形式: 直接值 或 {value:...} */
            let v = p[k];
            if (v && typeof v === 'object') v = v.value;
            out.properties[k] = v;
        });
        out.lastTime = out.lastTime || (propRes.data[0].time || 0);
    } else if (propRes && propRes.error) {
        out.error = propRes.error;
    } else if (propRes && propRes.errCode !== undefined) {
        out.error = 'OneNET errCode=' + propRes.errCode + ' ' + (propRes.desc || '');
    }

    return out;
}

async function setProps(properties) {
    const body = {
        device_name: ONENET_CONFIG.deviceName,
        properties: properties
    };
    const res = await onenetRequest('POST',
        '/thingmodel/properties/' + encodeURIComponent(ONENET_CONFIG.productID), body);

    if (res && res.errCode === 0) {
        return { ok: true };
    }
    if (res && res.error) {
        return { ok: false, error: res.error };
    }
    return { ok: false, error: 'OneNET errCode=' + (res && res.errCode) + ' ' + (res && (res.desc || '')) };
}

/* ================= HTTP 服务 ================= */
const server = http.createServer(function (req, res) {
    const sendJson = function (obj, code) {
        const body = JSON.stringify(obj);
        res.writeHead(code || 200, {
            'Content-Type': 'application/json; charset=utf-8',
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type'
        });
        res.end(body);
    };

    if (req.method === 'OPTIONS') {
        sendJson({ ok: true });
        return;
    }

    if (req.url === '/' || req.url === '/index.html') {
        fs.readFile(path.join(__dirname, 'index.html'), function (err, buf) {
            if (err) {
                res.writeHead(500);
                res.end('index.html not found');
                return;
            }
            res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
            res.end(buf);
        });
        return;
    }

    if (req.url === '/api/props' && req.method === 'GET') {
        getProps().then(sendJson);
        return;
    }

    if (req.url === '/api/props' && req.method === 'POST') {
        let chunks = '';
        req.on('data', function (c) { chunks += c; });
        req.on('end', function () {
            let body = {};
            try {
                body = JSON.parse(chunks || '{}');
            } catch (e) {
                sendJson({ ok: false, error: 'bad JSON' }, 400);
                return;
            }
            const properties = body.properties || {};
            if (!Object.keys(properties).length) {
                sendJson({ ok: false, error: 'properties is empty' }, 400);
                return;
            }
            /* 只允许下发受控属性 */
            const allowed = ['alarm_enable', 'count_reset', 'alarm_value'];
            const clean = {};
            Object.keys(properties).forEach(function (k) {
                if (allowed.indexOf(k) >= 0 && typeof properties[k] === 'number') {
                    clean[k] = properties[k];
                }
            });
            if (!Object.keys(clean).length) {
                sendJson({ ok: false, error: 'no valid properties (allowed: ' + allowed.join(',') + ')' }, 400);
                return;
            }
            log('set props: ' + JSON.stringify(clean));
            setProps(clean).then(sendJson);
        });
        return;
    }

    sendJson({ ok: false, error: 'not found' }, 404);
});

server.listen(ONENET_CONFIG.port, '0.0.0.0', function () {
    log('web console: http://127.0.0.1:' + ONENET_CONFIG.port);
    log('productID = ' + ONENET_CONFIG.productID);
    log('deviceName = ' + ONENET_CONFIG.deviceName);
    if (ONENET_CONFIG.apiKey.indexOf('在此填写') >= 0) {
        log('*** 警告: 尚未配置 OneNET API-KEY, 请在 server.js 或环境变量中填写 ***');
    }
});
