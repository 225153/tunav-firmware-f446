require('dotenv').config();
const express = require('express');
const mqtt    = require('mqtt');
const { createClient } = require('@supabase/supabase-js');
const cors    = require('cors');

const app = express();
app.use(cors());
app.use(express.json());

// ════════════════════════════════════════
// 0. VALIDATION DE LA CONFIGURATION
// ════════════════════════════════════════
const REQUIRED_ENV = ['SUPABASE_URL', 'SUPABASE_KEY', 'EMQX_HOST', 'EMQX_PORT', 'EMQX_USERNAME', 'EMQX_PASSWORD', 'EMQX_TOPIC'];
const missing = REQUIRED_ENV.filter((key) => !process.env[key]);
if (missing.length > 0) {
    console.error(`[CONFIG] Variables manquantes dans .env: ${missing.join(', ')}`);
    process.exit(1);
}

const PORT = parseInt(process.env.PORT, 10) || 3000;

// ════════════════════════════════════════
// 1. CONNEXION SUPABASE
// ════════════════════════════════════════
const supabase = createClient(
    process.env.SUPABASE_URL,
    process.env.SUPABASE_KEY
);
console.log('[SUPABASE] Client initialise');

// ════════════════════════════════════════
// 2. DECODAGE TRAME *VIG...#
// ════════════════════════════════════════
function decoderTrame(raw) {
    try {
        // Nettoyage
        raw = raw.replace('*', '').replace('#', '').trim();
        const parts = raw.split('&');

        console.log('[DECODE] Parties de la trame:', parts);

        if (parts.length < 6) {
            console.error('[DECODE] Trame incomplete, parties:', parts.length);
            return null;
        }

        // Extraction
        const imei     = parts[1];
        const dateStr  = parts[2].substring(1); // retire "D"
        const seuils   = parts[3].substring(1); // retire "S"
        const voiesStr = parts[4].substring(1); // retire "V"
        const status   = parts[5];

        // Decodage date : ddMMyyyyhhmmss → ISO
        const dd   = dateStr.substring(0, 2);
        const MM   = dateStr.substring(2, 4);
        const yyyy = dateStr.substring(4, 8);
        const hh   = dateStr.substring(8, 10);
        const mm   = dateStr.substring(10, 12);
        const ss   = dateStr.substring(12, 14);
        const timestamp = `${yyyy}-${MM}-${dd}T${hh}:${mm}:${ss}Z`;

        // Decodage seuils hex → decimal → /10
        const seuilHaut = parseInt(seuils.substring(0, 4), 16) / 10.0;
        const seuilBas  = parseInt(seuils.substring(4, 8), 16) / 10.0;

        // Decodage 7 voies (4 chars hex chacune → /10)
        const voies = [];
        for (let i = 0; i < 7; i++) {
            const hex = voiesStr.substring(i * 4, i * 4 + 4);
            voies.push(parseInt(hex, 16) / 10.0);
        }

        // Decodage status (defauts / depassements / alarmes)
        const defauts      = parseInt(status.substring(0, 4),  16);
        const depassements = parseInt(status.substring(4, 8),  16);
        const alarmes      = parseInt(status.substring(8, 12), 16);

        const result = {
            imei,
            timestamp,
            seuil_haut   : seuilHaut,
            seuil_bas    : seuilBas,
            voie1        : voies[0],
            voie2        : voies[1],
            voie3        : voies[2],
            voie4        : voies[3],
            voie5        : voies[4],
            voie6        : voies[5],
            voie7        : voies[6],
            defauts,
            depassements,
            alarmes
        };

        console.log('[DECODE] Resultat:', result);
        return result;

    } catch (err) {
        console.error('[DECODE] Erreur:', err.message);
        return null;
    }
}

// ════════════════════════════════════════
// 3. CONNEXION MQTT → EMQX (TLS)
// ════════════════════════════════════════
console.log(`[MQTT] Connexion a ${process.env.EMQX_HOST}:${process.env.EMQX_PORT}...`);

const mqttClient = mqtt.connect({
    host               : process.env.EMQX_HOST,
    port               : parseInt(process.env.EMQX_PORT, 10),
    protocol           : 'mqtts',        // TLS comme dans le firmware
    username           : process.env.EMQX_USERNAME,
    password           : process.env.EMQX_PASSWORD,
    clientId           : `vigi_server_${Date.now()}`,
    // NOTE: le firmware utilise seclevel=0 (pas de verification du certificat CA).
    // rejectUnauthorized:false reproduit ce meme choix cote serveur. A activer (true)
    // en production si le firmware est mis a jour pour valider le certificat EMQX.
    rejectUnauthorized : false,
    reconnectPeriod    : 5000,           // reconnexion auto toutes les 5s
    connectTimeout     : 10000
});

// Connexion reussie
mqttClient.on('connect', () => {
    console.log('[MQTT] Connecte a EMQX avec succes !');

    // Abonnement au topic de la carte STM32
    mqttClient.subscribe(process.env.EMQX_TOPIC, { qos: 0 }, (err) => {
        if (!err) {
            console.log(`[MQTT] Abonne au topic: ${process.env.EMQX_TOPIC}`);
        } else {
            console.error('[MQTT] Erreur abonnement:', err.message);
        }
    });
});

// Message recu depuis le STM32
mqttClient.on('message', async (topic, message) => {
    const raw = message.toString();
    console.log('\n══════════════════════════════════');
    console.log('[MQTT] Message recu sur topic:', topic);
    console.log('[MQTT] Contenu:', raw);

    // Verification que c'est bien une trame Vigi
    if (!raw.startsWith('*VIG')) {
        console.log('[MQTT] Message de test ignore:', raw);
        return;
    }

    // Decodage
    const data = decoderTrame(raw);
    if (!data) {
        console.error('[MQTT] Impossible de decoder la trame');
        return;
    }

    // Stockage dans Supabase
    console.log('[SUPABASE] Insertion en cours...');
    const { data: inserted, error } = await supabase
        .from('mesures')
        .insert([data])
        .select();

    if (error) {
        console.error('[SUPABASE] Erreur:', error.message);
    } else {
        console.log('[SUPABASE] Stocke avec succes, ID:', inserted[0].id);
    }
    console.log('══════════════════════════════════\n');
});

// Erreur MQTT
mqttClient.on('error', (err) => {
    console.error('[MQTT] Erreur:', err.message);
});

// Deconnexion
mqttClient.on('offline', () => {
    console.warn('[MQTT] Hors ligne, reconnexion automatique...');
});

// Reconnexion
mqttClient.on('reconnect', () => {
    console.log('[MQTT] Tentative de reconnexion...');
});

// ════════════════════════════════════════
// 4. API REST POUR ANGULAR
// ════════════════════════════════════════

// GET /api/mesures → 100 dernieres mesures
app.get('/api/mesures', async (req, res) => {
    console.log('[API] GET /api/mesures');
    const { data, error } = await supabase
        .from('mesures')
        .select('*')
        .order('timestamp', { ascending: false })
        .limit(100);

    if (error) {
        console.error('[API] Erreur:', error.message);
        return res.status(500).json({ error: error.message });
    }
    res.json(data);
});

// GET /api/mesures/derniere → derniere mesure
app.get('/api/mesures/derniere', async (req, res) => {
    console.log('[API] GET /api/mesures/derniere');
    const { data, error } = await supabase
        .from('mesures')
        .select('*')
        .order('timestamp', { ascending: false })
        .limit(1)
        .single();

    if (error) {
        console.error('[API] Erreur:', error.message);
        return res.status(500).json({ error: error.message });
    }
    res.json(data);
});

// GET /api/mesures/voies?limit=50 → historique pour graphes
app.get('/api/mesures/voies', async (req, res) => {
    const limit = parseInt(req.query.limit, 10) || 50;
    console.log(`[API] GET /api/mesures/voies?limit=${limit}`);

    const { data, error } = await supabase
        .from('mesures')
        .select('timestamp,voie1,voie2,voie3,voie4,voie5,voie6,voie7')
        .order('timestamp', { ascending: true })
        .limit(limit);

    if (error) {
        console.error('[API] Erreur:', error.message);
        return res.status(500).json({ error: error.message });
    }
    res.json(data);
});

// GET /api/mesures/alarmes → mesures avec alarmes
app.get('/api/mesures/alarmes', async (req, res) => {
    console.log('[API] GET /api/mesures/alarmes');
    const { data, error } = await supabase
        .from('mesures')
        .select('*')
        .gt('alarmes', 0)
        .order('timestamp', { ascending: false })
        .limit(50);

    if (error) {
        console.error('[API] Erreur:', error.message);
        return res.status(500).json({ error: error.message });
    }
    res.json(data);
});

// GET /api/status → etat du serveur
app.get('/api/status', (req, res) => {
    res.json({
        serveur  : 'en ligne',
        mqtt     : mqttClient.connected ? 'connecte' : 'deconnecte',
        topic    : process.env.EMQX_TOPIC,
        supabase : process.env.SUPABASE_URL
    });
});

// ════════════════════════════════════════
// 5. LANCEMENT DU SERVEUR
// ════════════════════════════════════════
app.listen(PORT, () => {
    console.log('\n══════════════════════════════════');
    console.log(`[API] Serveur demarre sur http://localhost:${PORT}`);
    console.log('[API] Endpoints disponibles:');
    console.log(`  GET http://localhost:${PORT}/api/status`);
    console.log(`  GET http://localhost:${PORT}/api/mesures`);
    console.log(`  GET http://localhost:${PORT}/api/mesures/derniere`);
    console.log(`  GET http://localhost:${PORT}/api/mesures/voies`);
    console.log(`  GET http://localhost:${PORT}/api/mesures/alarmes`);
    console.log('══════════════════════════════════\n');
});
