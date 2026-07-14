require('dotenv').config();
const { createClient } = require('@supabase/supabase-js');

// ════════════════════════════════════════
// 1. CONFIGURATION
// ════════════════════════════════════════
const SUPABASE_URL = process.env.SUPABASE_URL;
const SUPABASE_KEY = process.env.SUPABASE_KEY;
const INTERVAL_MS = parseInt(process.env.INTERVAL_MS, 10) || 3000;
const DEVICE_IMEIS = (process.env.DEVICE_IMEIS || '864369039705391')
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean);

if (!SUPABASE_URL || !SUPABASE_KEY) {
    console.error('[SIMULATEUR] SUPABASE_URL et SUPABASE_KEY sont requis (voir .env.example)');
    process.exit(1);
}

const supabase = createClient(SUPABASE_URL, SUPABASE_KEY);
console.log('[SUPABASE] Client initialise');

const VOIE_COUNT = 7;
const SEUIL_HAUT = 10.8;
const SEUIL_BAS = 6.3;

// ════════════════════════════════════════
// 2. GENERATION DE DONNEES (random-walk par appareil)
// ════════════════════════════════════════
const deviceState = new Map();

function getState(imei) {
    if (!deviceState.has(imei)) {
        deviceState.set(
            imei,
            Array.from({ length: VOIE_COUNT }, () => randomBetween(5, 8))
        );
    }
    return deviceState.get(imei);
}

function randomBetween(min, max) {
    return Math.random() * (max - min) + min;
}

function round1(n) {
    return Math.round(n * 10) / 10;
}

// Fait evoluer chaque voie legerement (courbe realiste), avec des variations plus frequentes
// pour declencher des depassements/alarmes et des defauts internes de facon plus realiste.
function nextVoieValue(current) {
    const step = randomBetween(-0.9, 0.9);
    return Math.min(Math.max(current + step, 0), 20);
}

function buildScenario() {
    const scenario = { high: [], low: [] };
    const roll = Math.random();

    if (roll < 0.25) {
        scenario.high.push(Math.floor(Math.random() * VOIE_COUNT));
    } else if (roll < 0.5) {
        scenario.low.push(Math.floor(Math.random() * VOIE_COUNT));
    } else if (roll < 0.8) {
        const first = Math.floor(Math.random() * VOIE_COUNT);
        const second = (first + 1 + Math.floor(Math.random() * (VOIE_COUNT - 1))) % VOIE_COUNT;
        scenario.high.push(first);
        scenario.low.push(second);
    } else {
        const count = 1 + Math.floor(Math.random() * 2);
        for (let i = 0; i < count; i += 1) {
            const idx = Math.floor(Math.random() * VOIE_COUNT);
            if (Math.random() < 0.5) scenario.high.push(idx);
            else scenario.low.push(idx);
        }
    }

    return scenario;
}

function applyScenario(voies, scenario) {
    return voies.map((value, idx) => {
        let nextValue = value;
        if (scenario.high.includes(idx)) {
            nextValue += randomBetween(2.5, 4.5);
        }
        if (scenario.low.includes(idx)) {
            nextValue -= randomBetween(2.2, 4.2);
        }
        return Math.min(Math.max(nextValue, 0), 20);
    });
}

// Encode depassements/alarmes en respectant le mapping de bits du protocole VIGI :
//   depassements bit (N-1)   = depassement seuil haut voie N
//   depassements bit (N-1+8) = depassement seuil bas voie N
//   alarmes bit (N-1)        = alarme voie N
function encodeStatus(voies) {
    let depassements = 0;
    let alarmes = 0;

    voies.forEach((v, idx) => {
        if (v > SEUIL_HAUT) {
            depassements |= 1 << idx;
            alarmes |= 1 << idx;
        }
        if (v < SEUIL_BAS) {
            depassements |= 1 << (idx + 8);
        }
    });

    // Defauts internes : plusieurs bits peuvent etre actifs simultanement.
    let defauts = 0;
    if (Math.random() < 0.2) {
        const availableBits = [6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        const count = 1 + Math.floor(Math.random() * 2);
        for (let i = 0; i < count; i += 1) {
            const pickIndex = Math.floor(Math.random() * availableBits.length);
            const bit = availableBits.splice(pickIndex, 1)[0];
            defauts |= 1 << bit;
        }
    }

    return { depassements, alarmes, defauts };
}

function generateMesure(imei) {
    const state = getState(imei);
    const baseVoies = state.map(nextVoieValue);
    const scenario = buildScenario();
    const voies = applyScenario(baseVoies, scenario);
    deviceState.set(imei, voies);

    const { depassements, alarmes, defauts } = encodeStatus(voies);

    return {
        imei,
        timestamp: new Date().toISOString(),
        seuil_haut: SEUIL_HAUT,
        seuil_bas: SEUIL_BAS,
        voie1: round1(voies[0]),
        voie2: round1(voies[1]),
        voie3: round1(voies[2]),
        voie4: round1(voies[3]),
        voie5: round1(voies[4]),
        voie6: round1(voies[5]),
        voie7: round1(voies[6]),
        defauts,
        depassements,
        alarmes,
    };
}

// ════════════════════════════════════════
// 3. INSERTION DANS SUPABASE
// ════════════════════════════════════════
async function insertMesure(imei) {
    const mesure = generateMesure(imei);

    const { data, error } = await supabase.from('mesures').insert([mesure]).select();

    if (error) {
        console.error(`[SUPABASE] Erreur insertion (${imei}):`, error.message);
        return;
    }

    const voiesStr = [1, 2, 3, 4, 5, 6, 7].map((i) => mesure[`voie${i}`]).join(', ');
    console.log(
        `[SUPABASE] Insere id=${data[0].id} imei=${imei} voies=[${voiesStr}] alarmes=${mesure.alarmes}`
    );
}

// ════════════════════════════════════════
// 4. BOUCLE DE SIMULATION
// ════════════════════════════════════════
let deviceIndex = 0;
function tick() {
    const imei = DEVICE_IMEIS[deviceIndex % DEVICE_IMEIS.length];
    deviceIndex += 1;
    insertMesure(imei);
}

console.log(`[SIMULATEUR] Demarrage - une insertion toutes les ${INTERVAL_MS}ms`);
console.log(`[SIMULATEUR] Appareils simules: ${DEVICE_IMEIS.join(', ')}`);

tick(); // premiere insertion immediate
const timer = setInterval(tick, INTERVAL_MS);

process.on('SIGINT', () => {
    console.log('\n[SIMULATEUR] Arret demande, fermeture...');
    clearInterval(timer);
    process.exit(0);
});
