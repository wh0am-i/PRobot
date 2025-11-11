#include <WiFi.h>
#include <WebServer.h>
#include "driver/ledc.h"

// --- Pinos dos Motores ---
const int IN1_E = 5;
const int IN2_E = 17;
const int PWM_E_PIN = 15;

const int IN1_D = 18;
const int IN2_D = 19;
const int PWM_D_PIN = 2;

// --- Configurações de Rede ---
WebServer server(80);
const char *ssid = "P-Robot";
const char *password = "qualsenha";

// --- Configuração PWM ---
const int FREQ_PWM = 5000;
const ledc_timer_bit_t RESOLUCAO_PWM = LEDC_TIMER_8_BIT;
const ledc_channel_t CANAL_PWM_E = LEDC_CHANNEL_0;
const ledc_channel_t CANAL_PWM_D = LEDC_CHANNEL_1;

// --- Estado do Robô ---
int direcaoAtual = -1;

// Protótipo da função
void processaComando(String comando);

// --- Página HTML ---
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>P-Robot Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            display: flex; 
            flex-direction: column; 
            align-items: center; 
            height: 100vh; 
            margin: 0; 
            padding: 0;
            background-color: #2c3e50; 
            color: white; 
        }
        .joystick-container { 
            display: flex; 
            gap: 70px; /* Ajuste */
            justify-content: center; /* Centraliza os joysticks */
            align-items: center;
        }
        .joystick { 
            width: 250px; 
            height: 250px; 
            background-color: #34495e; 
            border-radius: 50%; 
            position: relative; 
            flex-shrink: 0; 
            touch-action: none; /* Impede que o navegador role a tela ao arrastar o joystick */
        }
        .handle { 
            width: 55px; 
            height: 55px; 
            background-color: #95a5a6; 
            border-radius: 50%; 
            position: absolute; 
            left: 97.5px; 
            top: 97.5px; 
            transform: translate(0,0); 
            transition: transform 0.05s; 
        }
    </style>
</head>
<body>
    <div class="joystick-container">
        <div class="joystick" id="joystickV"><div class="handle"></div></div>
        <div class="joystick" id="joystickH"><div class="handle"></div></div>
    </div>

<script>
    const joystickV = document.getElementById('joystickV');
    const joystickH = document.getElementById('joystickH');
    let vertical = 0, horizontal = 0;
    let ultimoComando = '', envioIntervalo = null;
    const FREQ_ENVIO_MS = 100; // Taxa de envio dos comandos

    // Objeto para mapear touch.identifier para o joystick sendo controlado
    const activeTouches = {};

    // Função geradora do comando a partir das posições dos joysticks
    function gerarComando() {
        let v_abs = Math.abs(vertical);
        let h_abs = Math.abs(horizontal);

        // --- Lógica de ativação de velocidade ---
        let vel;
        if (v_abs <= 10) { // Se estiver na zona morta
            vel = 0;
        } else {
            // Mapeia v_abs de [11, 100] para [60, 100]
            vel = Math.round((v_abs - 11) * (100 - 60) / (100 - 11) + 60);
            vel = Math.min(100, Math.max(60, vel)); // Garante que fique entre 60 e 100
        }

        let curva;
        if (h_abs <= 10) { // Se estiver na zona morta
            curva = 0;
        } else {
            // Mapeia h_abs de [11, 100] para [60, 100]
            curva = Math.round((h_abs - 11) * (100 - 60) / (100 - 11) + 60);
            curva = Math.min(100, Math.max(60, curva)); // Garante que fique entre 60 e 100
        }
        
        // Se ambos estão na zona morta, retorna o comando de parada
        if (vel === 0 && curva === 0) return "000F000N";

        let direcao = vertical >= 0 ? 'F' : 'B'; 
        
        let lado = 'N'; // Neutro por padrão
        if (horizontal > 10) lado = 'D';
        else if (horizontal < -10) lado = 'E';
        
        return `${String(vel).padStart(3, '0')}${direcao}${String(curva).padStart(3, '0')}${lado}`;
    }

    // Envia o comando para o ESP32 se ele for diferente do último enviado
    function enviarComando() {
        const comando = gerarComando();
        if (comando !== ultimoComando) {
            fetch(`/comando?cmd=${comando}`);
            ultimoComando = comando;
        }
    }

    // Inicia o envio periódico de comandos
    function iniciarEnvio() {
        if (!envioIntervalo) {
            envioIntervalo = setInterval(enviarComando, FREQ_ENVIO_MS);
        }
    }

    // Para o envio periódico e manda um último comando de parada para garantir
    function pararEnvio() {
        clearInterval(envioIntervalo);
        envioIntervalo = null;
        // Envia comando de parada APENAS se os dois joysticks não estiverem sendo usados
        if (Object.keys(activeTouches).length === 0) { 
            fetch(`/comando?cmd=000F000N`); 
            ultimoComando = '000F000N';
        }
    }

    function handleMove(e, joystick, isVertical) {
        // Encontra o toque correto para este joystick
        let touch = null;
        for (let i = 0; i < e.changedTouches.length; i++) {
            if (activeTouches[e.changedTouches[i].identifier] === joystick) {
                touch = e.changedTouches[i];
                break;
            }
        }
        if (!touch) return; // Se o toque não é para este joystick, ignora

        const rect = joystick.getBoundingClientRect();
        const handle = joystick.querySelector('.handle');
        
        let dx = touch.clientX - (rect.left + rect.width / 2);
        let dy = touch.clientY - (rect.top + rect.height / 2);
        const maxDist = rect.width / 2 - handle.offsetWidth / 2; // maxDist é calculado dinamicamente

        if (isVertical) {
            const moveY = Math.max(-maxDist, Math.min(maxDist, dy));
            handle.style.transform = `translateY(${moveY}px)`;
            vertical = (-moveY / maxDist) * 100;
        } else {
            const moveX = Math.max(-maxDist, Math.min(maxDist, dx));
            handle.style.transform = `translateX(${moveX}px)`;
            horizontal = (moveX / maxDist) * 100;
        }
    }

    function resetJoystick(joystick, isVertical) {
        // Remove o toque ativo para este joystick
        for (const id in activeTouches) {
            if (activeTouches[id] === joystick) {
                delete activeTouches[id];
            }
        }

        joystick.querySelector('.handle').style.transform = 'translate(0, 0)';
        if (isVertical) vertical = 0;
        else horizontal = 0;
        
        // Apenas para o envio se ambos os joysticks estiverem soltos
        if (Object.keys(activeTouches).length === 0 && vertical === 0 && horizontal === 0) {
            pararEnvio();
        }
    }

    // Configuração dos Eventos de Toque
    [joystickV, joystickH].forEach(joy => {
        const isVertical = (joy === joystickV);
        joy.addEventListener('touchstart', (e) => { 
            e.preventDefault(); 
            // Registra o toque inicial para este joystick
            for (let i = 0; i < e.changedTouches.length; i++) {
                activeTouches[e.changedTouches[i].identifier] = joy;
            }
            iniciarEnvio(); 
            handleMove(e, joy, isVertical); 
        }, { passive: false });

        joy.addEventListener('touchmove', (e) => { 
            e.preventDefault(); 
            handleMove(e, joy, isVertical); 
        }, { passive: false });

        joy.addEventListener('touchend', (e) => { 
            e.preventDefault(); 
            // Remove os toques que terminaram
            for (let i = 0; i < e.changedTouches.length; i++) {
                if (activeTouches[e.changedTouches[i].identifier] === joy) {
                    resetJoystick(joy, isVertical); // Reseta o joystick específico
                }
            }
        });
    });
</script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);

    pinMode(IN1_E, OUTPUT);
    pinMode(IN2_E, OUTPUT);
    pinMode(IN1_D, OUTPUT);
    pinMode(IN2_D, OUTPUT);

    ledc_timer_config_t timer_conf = {.speed_mode = LEDC_HIGH_SPEED_MODE, .duty_resolution = RESOLUCAO_PWM, .timer_num = LEDC_TIMER_0, .freq_hz = FREQ_PWM, .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf_e = {.gpio_num = PWM_E_PIN, .speed_mode = LEDC_HIGH_SPEED_MODE, .channel = CANAL_PWM_E, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0};
    ledc_channel_config(&channel_conf_e);

    ledc_channel_config_t channel_conf_d = {.gpio_num = PWM_D_PIN, .speed_mode = LEDC_HIGH_SPEED_MODE, .channel = CANAL_PWM_D, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0};
    ledc_channel_config(&channel_conf_d);

    WiFi.softAP(ssid, password);
    Serial.println("Rede Wi-Fi '" + String(ssid) + "' iniciada.");
    Serial.print("IP: http://");
    Serial.println(WiFi.softAPIP());

    server.on("/", []() { server.send(200, "text/html", htmlPage); });
    server.on("/comando", []() {
        if (server.hasArg("cmd")) processaComando(server.arg("cmd"));
        server.send(204);
    });

    server.begin();
    Serial.println("Servidor HTTP iniciado.");
}

void loop() {
    server.handleClient();
}

void pararMotores() {
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_E, 0);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_D, 0);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_E);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_D);
    direcaoAtual = -1;
}

void mover(int pwmE, int pwmD, int novaDirecao) {
    // Proteção: se a direção for invertida, para os motores primeiro
    if (novaDirecao != direcaoAtual && direcaoAtual != -1) {
        pararMotores();
        delay(50); // Pausa CRÍTICA para evitar surto de corrente nos motores
    }
    direcaoAtual = novaDirecao;

    // novaDirecao == 0 significa FRENTE
    if (novaDirecao == 0) { 
        // Motor Esquerdo
        digitalWrite(IN1_E, HIGH); 
        digitalWrite(IN2_E, LOW);  
        
        // Motor Direito
        digitalWrite(IN1_D, HIGH); 
        digitalWrite(IN2_D, LOW);  
    } 
    // novaDirecao == 1 significa TRÁS
    else { 
        // Motor Esquerdo
        digitalWrite(IN1_E, LOW);  
        digitalWrite(IN2_E, HIGH); 
        
        // Motor Direito
        digitalWrite(IN1_D, LOW);  
        digitalWrite(IN2_D, HIGH); 
    }

    ledc_set_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_E, pwmE);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_D, pwmD);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_E);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, CANAL_PWM_D);
}

void processaComando(String comando) {
    if (comando.length() != 8) return;

    int vel = comando.substring(0, 3).toInt();
    int curva = comando.substring(4, 7).toInt();

    if (vel == 0 && curva == 0) {
        pararMotores();
        return;
    }

    char direcaoChar = comando.charAt(3);
    char ladoChar = comando.charAt(7);
    
    float velBase = map(vel, 0, 100, 80, 255);
    float fatorCurva = curva / 100.0;

    float pwmE = velBase;
    float pwmD = velBase;

    // Aplica a curva reduzindo a velocidade de um dos motores
    if (ladoChar == 'E') pwmE *= (1.0 - fatorCurva);
    else if (ladoChar == 'D') pwmD *= (1.0 - fatorCurva); 
    
    // Diminui a potência do motor da esquerda em 8.5%
    pwmE *= 0.92; 
    // Diminui a potência do motor da direita em 1%
    //pwmD *= 0.99; 

    int novaDirecao = (direcaoChar == 'F') ? 0 : 1; 
    
    mover(constrain(pwmE, 0, 255), constrain(pwmD, 0, 255), novaDirecao);

}
