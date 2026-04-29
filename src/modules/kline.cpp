#include "kline.h"
#include "core/message.h"
#include "core/logging.h"

// ============================================================================
// Инициализация
// ============================================================================

/**
 * @brief Инициализация модуля KLine.
 * 
 * Выполняет начальную настройку:
 * - Обнуление структуры данных (pack) и установку версии.
 * - Установку начальных значений по умолчанию (температуры, передача, коды ошибок).
 * - Инициализацию UART2, если включен реальный режим (_realMode=true).
 */
bool KLine::onInit() {
    // Обнуление структуры KlinePack, которая хранит все данные для публикации.
    memset(&pack, 0, sizeof(pack));
    // Установка версии пакета.
    pack.version           = 2;
    // Установка начальных значений по умолчанию.
    pack.coolant_temp      = 90.0f;
    pack.atf_temp          = 75.0f;
    pack.selector_position = 3;
    pack.current_gear      = 2;
    // Установка начальных кодов ошибок (DTC).
    strcpy(pack.dtc_codes, "P0135;P0141");
    
    // Если включен реальный режим (работа с настоящим ЭБУ), инициализируем UART.
    if (_realMode) {
        uartInit();
        LOG_INFO(name, "Init (real K-Line)");
    } else {
        // В противном случае, мы используем симуляцию.
        LOG_INFO(name, "Init (simulation)");
    }
    return true;
}

/**
 * @brief Инициализация UART2 для связи по K-Line.
 * 
 * Настраивает Serial2 на скорость 10400 бод, формат 8N1 и заданные пины GPIO.
 */
void KLine::uartInit() {
    // Устанавливаем указатель на Serial2.
    _uart = &Serial2;
    // Инициализируем Serial2 с заданными параметрами.
    _uart->begin(10400, SERIAL_8N1, KLINE_RX, KLINE_TX);
    // Устанавливаем размер буфера приема.
    _uart->setRxBufferSize(KLINE_RX_BUF);
    // Записываем сообщение в лог.
    LOG_INFO(name, "UART2: 10400 baud");
}

// ============================================================================
// Основной цикл
// ============================================================================

/**
 * @brief Основной цикл обработки модуля.
 * 
 * Выполняет:
 * - Инициализацию соединения, если оно не установлено (в реальном режиме).
 * - Периодическое опрос данных с ЭБУ (в реальном режиме) или генерацию случайных данных (в симуляции).
 * - Обработку команд, поступивших через onCommand (например, чтение DTC или сброс адаптации TCM).
 * - Публикацию обновленных данных в топик Topic::KLINE раз в секунду.
 */
void KLine::onProcess() {
    unsigned long now = millis();
    
    // Режим реальной работы
    if (_realMode) {
        // Если соединение еще не установлено, запускаем процесс инициализации.
        if (!_connected) {
            processInit();
            return;
        }
        
        // Публикация данных раз в 1000 мс.
        if (now - lastPublish >= 1000) {
            lastPublish = now;
            
            // Чтение текущих значений с ЭБУ.
            pack.coolant_temp      = readCoolantTemp();
            pack.atf_temp          = readAtfTemp();
            pack.voltage           = readVoltage();
            pack.fuel_percent      = readFuelPercent();
            pack.output_shaft_rpm  = readOutputShaftRpm();
            readGear(); // Эта функция обновляет pack.selector_position, pack.current_gear и pack.tcc_lockup.
            
            // Обработка команд, поступивших через onCommand.
            // Запросить коды ошибок (DTC).
            if (_cmd.readDtc) {
                _cmd.readDtc = false;
                pack.dtc_count = readDtcCodes();
            }
            // Сбросить коды ошибок.
            if (_cmd.clearDtc) {
                _cmd.clearDtc = false;
                if (clearDtc()) {
                    pack.dtc_count = 0;
                    memset(pack.dtc_codes, 0, sizeof(pack.dtc_codes));
                }
            }
            // Сбросить адаптацию TCM.
            if (_cmd.resetTcm) {
                _cmd.resetTcm = false;
                resetTcmAdaptation();
            }
            // Быстрая инициализация (диагностика протокола).
            if (_cmd.detectProto) {
                _cmd.detectProto = false;
                fastInit();
            }
            
            // Публикация всех собранных данных другим модулям (например, Protocol).
            publish(Topic::KLINE, &pack, sizeof(pack));
        }
    } 
    // Режим симуляции (для тестирования без подключения к ЭБУ)
    else {
        // Публикация случайных данных раз в 1000 мс.
        if (now - lastPublish >= 1000) {
            lastPublish = now;
            simulateData();
            publish(Topic::KLINE, &pack, sizeof(pack));
        }
    }
}

// ============================================================================
// Команды
// ============================================================================

/**
 * @brief Обработка входящих команд.
 * 
 * Реагирует на команды, отправленные в шину Topic::SYSTEM.
 * Устанавливает флаги в структуре _cmd, которые будут обработаны в onProcess().
 */
void KLine::onCommand(const CommandMsg& cmd) {
    switch (cmd.cmd) {
        // Запросить коды неисправностей (DTC)
        case CMD_KL_GET_DTC:    
            if (_realMode) _cmd.readDtc = true;    
            break;
        // Сбросить коды неисправностей (DTC)
        case CMD_KL_CLEAR_DTC:  
            if (_realMode) _cmd.clearDtc = true;  
            else { 
                // В режиме симуляции просто сбрасываем коды в пакете.
                pack.dtc_count = 0; 
                memset(pack.dtc_codes, 0, sizeof(pack.dtc_codes)); 
            }
            break;
        // Сброс адаптации TCM (автоматической коробки передач)
        case CMD_KL_RESET_ADAPT: 
            if (_realMode) _cmd.resetTcm = true;  
            LOG_INFO(name, "TCM reset"); 
            break;
        // Запустить насос ATF (не реализовано в коде)
        case CMD_KL_PUMP_ATF:   
            if (_realMode) _cmd.pumpAbs = true;   
            LOG_INFO(name, "ATF pump"); 
            break;
        // Быстрая инициализация для определения протокола
        case CMD_KL_DETECT_PROTO: 
            if (_realMode) _cmd.detectProto = true; 
            LOG_INFO(name, "Proto detect"); 
            break;
    }
}

// ============================================================================
// Конечный автомат инициализации
// ============================================================================

/**
 * @brief Запуск процесса инициализации по протоколу ISO 9141-2.
 * 
 * Устанавливает начальное состояние конечного автомата (WAIT_POWER_ON),
 * сбрасывает таймер, счетчик попыток и логирует начало процесса.
 */
void KLine::startInit() {
    _state = KlineState::WAIT_POWER_ON;
    _stateTimer = millis();
    _retries = 0;
    LOG_INFO(name, "Starting init...");
}

/**
 * @brief Обработка конечного автомата инициализации.
 * 
 * Реализует последовательность шагов для установки соединения с ЭБУ:
 * 1. Ожидание подачи питания (300 мс).
 * 2. Отправка низкого уровня (5 мс) для сигнала K.
 * 3. Отправка битовой последовательности адреса (11011).
 * 4. Ожидание синхронизационного байта (0x55).
 * 5. Ожидание двух ключевых байтов (0x8F или 0xEF).
 * 6. Прием идентификатора ЭБУ.
 * 
 * В случае ошибки, автомат перезапускается до KLINE_INIT_RETRIES раз.
 */
void KLine::processInit() {
    unsigned long now = millis();
    
    switch (_state) {
        // Состояние IDLE - запускаем инициализацию.
        case KlineState::IDLE: startInit(); break;
            
        // Состояние: ожидание подачи питания.
        case KlineState::WAIT_POWER_ON:
            if (now - _stateTimer >= 300) { 
                _state = KlineState::SEND_ADDR_LOW; 
                _stateTimer = now; 
            }
            break;
            
        // Состояние: отправка низкого уровня на линию K.
        case KlineState::SEND_ADDR_LOW:
            // Очищаем буфер UART.
            _uart->flush();
            // Настраиваем выход и устанавливаем низкий уровень.
            pinMode(KLINE_TX, OUTPUT);
            digitalWrite(KLINE_TX, LOW);
            // Ждем 25 мс (таймаут W1).
            if (now - _stateTimer >= 25) {
                // Возвращаем пин в режим INPUT_PULLUP (высокий уровень).
                pinMode(KLINE_TX, INPUT_PULLUP);
                // Переход к отправке битов адреса.
                _state = KlineState::SEND_ADDR_BITS;
                _addrBitIdx = 0;
                _stateTimer = now;
            }
            break;
            
        // Состояние: отправка битов адреса (1-1-0-1-1) по одному.
        case KlineState::SEND_ADDR_BITS: {
            static const uint8_t bits[] = {1, 1, 0, 1, 1};
            // Отправка бита каждые 200 мс.
            if (_addrBitIdx < 5 && now - _stateTimer >= 200) {
                pinMode(KLINE_TX, OUTPUT);
                digitalWrite(KLINE_TX, bits[_addrBitIdx] ? HIGH : LOW);
                _addrBitIdx++;
                _stateTimer = now;
            }
            // После отправки всех 5 битов.
            if (_addrBitIdx >= 5) {
                // Возвращаем пин в режим INPUT_PULLUP.
                pinMode(KLINE_TX, INPUT_PULLUP);
                // Повторно инициализируем UART для приема на 10400 бод.
                _uart->begin(10400, SERIAL_8N1, KLINE_RX, KLINE_TX);
                // Переход к ожиданию синхронизации.
                _state = KlineState::WAIT_SYNC;
                _stateTimer = now;
            }
            break;
        }
            
        // Состояние: ожидание синхронизационного байта (0x55).
        case KlineState::WAIT_SYNC:
            if (now - _stateTimer > KLINE_W1_TIMEOUT) {
                uint8_t b = rxByte(10);
                // Если получен правильный байт, переходим к следующему состоянию.
                _state = (b == 0x55) ? KlineState::WAIT_KEY_BYTE1 : KlineState::FAILED;
                _stateTimer = now;
            }
            break;
            
        // Состояние: ожидание первого ключевого байта (0x8F или 0xEF).
        case KlineState::WAIT_KEY_BYTE1:
            if (now - _stateTimer > KLINE_W4_TIMEOUT) {
                uint8_t b = rxByte(KLINE_W1_TIMEOUT);
                _state = (b == 0x8F || b == 0xEF) ? KlineState::WAIT_KEY_BYTE2 : KlineState::FAILED;
                _stateTimer = now;
            }
            break;
            
        // Состояние: ожидание второго ключевого байта (0x8F или 0xEF).
        case KlineState::WAIT_KEY_BYTE2:
            if (now - _stateTimer > KLINE_W4_TIMEOUT) {
                uint8_t b = rxByte(KLINE_W1_TIMEOUT);
                _state = (b == 0x8F || b == 0xEF) ? KlineState::WAIT_ECU_ID : KlineState::FAILED;
                _stateTimer = now;
            }
            break;
            
        // Состояние: ожидание идентификатора ЭБУ.
        case KlineState::WAIT_ECU_ID:
            if (now - _stateTimer > KLINE_W4_TIMEOUT) {
                _ecuId = rxByte(KLINE_W1_TIMEOUT);
                _connected = true;
                _state = KlineState::CONNECTED;
                LOG_INFO(name, "Connected! ECU ID: 0x%02X", _ecuId);
            }
            break;
            
        // Состояние: инициализация не удалась.
        case KlineState::FAILED:
            _retries++;
            // Повторяем инициализацию, если попытки еще есть.
            if (_retries < KLINE_INIT_RETRIES) startInit();
            else { 
                // Если попытки закончились, сообщаем об ошибке и возвращаемся в состояние IDLE.
                LOG_ERROR(name, "Init failed"); 
                _state = KlineState::IDLE; 
            }
            break;
            
        default: break;
    }
}

// ============================================================================
// Низкоуровневые операции
// ============================================================================

/**
 * @brief Отправка одного байта по UART.
 * 
 * @param b Байт для отправки.
 */
void KLine::txByte(uint8_t b) { 
    _uart->write(b); 
    _uart->flush(); // Ждем, пока байт будет отправлен.
}

/**
 * @brief Чтение одного байта из UART с таймаутом.
 * 
 * @param timeoutMs Максимальное время ожидания в миллисекундах.
 * @return Прочитанный байт или 0, если таймаут истек.
 */
uint8_t KLine::rxByte(unsigned long timeoutMs) {
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (_uart->available()) return _uart->read();
        delay(1);
    }
    return 0;
}

/**
 * @brief Отправка запроса (фрейма) на ЭБУ.
 * 
 * Формирует пакет запроса с указанным адресом ЭБУ, режимом и PID,
 * вычисляет и добавляет контрольную сумму, затем отправляет пакет.
 * 
 * @param ecu Адрес ЭБУ (KLINE_ECU_ECM или KLINE_ECU_TCM).
 * @param mode Режим запроса (например, 0x01 для стандартных данных OBD2).
 * @param pid Идентификатор параметра (например, 0x05 для температуры ОЖ).
 */
void KLine::sendFrame(uint16_t ecu, uint8_t mode, uint16_t pid) {
    uint8_t idx = 0;
    // Длина пакета: 2 байта (режим, PID) или 3 байта (режим, PID старший, PID младший).
    _txBuf[idx++] = (pid > 0xFF) ? 3 : 2;
    // Код режима.
    _txBuf[idx++] = mode;
    // Старший байт PID, если он двухбайтовый.
    if (pid > 0xFF) { _txBuf[idx++] = (pid >> 8) & 0xFF; }
    // Младший байт PID.
    _txBuf[idx++] = pid & 0xFF;
    // Контрольная сумма (сумма всех байтов до этого).
    _txBuf[idx++] = checksum(_txBuf, idx);
    // Отправка сформированного пакета по UART.
    _uart->write(_txBuf, idx);
    _uart->flush();
}

/**
 * @brief Вычисление контрольной суммы по протоколу ISO 9141-2.
 * 
 * @param data Указатель на массив данных.
 * @param len Длина массива.
 * @return Контрольная сумма (сумма всех байтов).
 */
uint8_t KLine::checksum(const uint8_t* data, int len) {
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += data[i];
    return sum;
}

/**
 * @brief Чтение входящего пакета (фрейма) с таймаутом.
 * 
 * Собирает все доступные байты в течение заданного времени в буфер _rxBuf.
 * 
 * @param timeoutMs Таймаут ожидания в миллисекундах.
 * @return Количество прочитанных байт.
 */
int KLine::readFrame(unsigned long timeoutMs) {
    _rxLen = 0;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (_uart->available()) {
            _rxBuf[_rxLen++] = _uart->read();
            start = millis(); // Обновляем таймер при каждом байте.
            if (_rxLen >= KLINE_RX_BUF) break; // Защита от переполнения.
        }
    }
    return _rxLen;
}

// ============================================================================
// Запросы данных
// ============================================================================

/**
 * @brief Запросить температуру охлаждающей жидкости.
 * @return Температура в градусах Цельсия или последнее известное значение.
 */
float KLine::readCoolantTemp() {
    // Отправляем запрос: ЭБУ=ECM, режим=0x01 (OBD2), PID=0x05 (Coolant Temperature).
    sendFrame(KLINE_ECU_ECM, 0x01, 0x05);
    // Ждем ответ (максимум 50 мс).
    if (readFrame(50) >= 3) 
        // Температура = байт[2] - 40°C.
        return _rxBuf[2] - 40.0f;
    // Если ответ не пришел или поврежден, возвращаем последнее значение.
    return pack.coolant_temp;
}

/**
 * @brief Запросить температуру масла АКПП (ATF).
 * @return Температура в градусах Цельсия или последнее известное значение.
 */
float KLine::readAtfTemp() {
    // Отправляем запрос: ЭБУ=ECM, режим=0x21 (расширенный Toyota), PID=0x6C.
    sendFrame(KLINE_ECU_ECM, 0x21, 0x6C);
    if (readFrame(50) >= 3) 
        return _rxBuf[2] - 40.0f;
    return pack.atf_temp;
}

/**
 * @brief Запросить напряжение бортовой сети.
 * @return Напряжение в вольтах или последнее известное значение.
 */
float KLine::readVoltage() {
    // Отправляем запрос: ЭБУ=ECM, режим=0x01 (OBD2), PID=0x42 (Control Module Voltage).
    sendFrame(KLINE_ECU_ECM, 0x01, 0x42);
    if (readFrame(50) >= 4) 
        // Напряжение = (байт[2] * 256 + байт[3]) / 1000.
        return ((_rxBuf[2] * 256.0f) + _rxBuf[3]) / 1000.0f;
    return pack.voltage;
}

/**
 * @brief Запросить уровень топлива в процентах.
 * @return Уровень топлива в процентах или последнее известное значение.
 */
float KLine::readFuelPercent() {
    // Отправляем запрос: ЭБУ=ECM, режим=0x01 (OBD2), PID=0x2F (Fuel Level Input).
    sendFrame(KLINE_ECU_ECM, 0x01, 0x2F);
    if (readFrame(50) >= 3) 
        // Уровень = байт[2] * 100 / 255.
        return _rxBuf[2] * 100.0f / 255.0f;
    return pack.fuel_percent;
}

/**
 * @brief Запросить частоту вращения выходного вала АКПП.
 * @return Частота вращения в об/мин или последнее известное значение.
 */
float KLine::readOutputShaftRpm() {
    // Отправляем запрос: ЭБУ=TCM, режим=0x21 (расширенный Toyota), PID=0x8A.
    sendFrame(KLINE_ECU_TCM, 0x21, 0x8A);
    if (readFrame(50) >= 4) 
        // Частота = (байт[2] * 256 + байт[3]) * 0.25.
        return ((_rxBuf[2] * 256.0f) + _rxBuf[3]) * 0.25f;
    return pack.output_shaft_rpm;
}

/**
 * @brief Запросить положение селектора и текущую передачу.
 * @return true, если данные успешно получены, false в случае ошибки.
 */
bool KLine::readGear() {
    // Отправляем запрос: ЭБУ=ECM, режим=0x21 (расширенный Toyota), PID=0x41.
    sendFrame(KLINE_ECU_ECM, 0x21, 0x41);
    if (readFrame(50) >= 5) {
        // Обновляем поля в структуре pack.
        pack.selector_position = _rxBuf[2];
        pack.current_gear = _rxBuf[3];
        pack.tcc_lockup = (_rxBuf[4] != 0);
        return true;
    }
    return false;
}

/**
 * @brief Запросить коды неисправностей (DTC).
 * @return Количество кодов или последнее известное количество.
 */
int KLine::readDtcCodes() {
    // Отправляем запрос: ЭБУ=ECM, режим=0x03 (Request Stored DTCs).
    sendFrame(KLINE_ECU_ECM, 0x03, 0);
    // Ответ содержит количество байтов > 2. Количество DTC = (длина - 1) / 2.
    if (readFrame(50) > 2) return (_rxLen - 1) / 2;
    return pack.dtc_count;
}

/**
 * @brief Сбросить коды неисправностей (DTC).
 * @return true при успешной отправке и подтверждении, false при ошибке.
 */
bool KLine::clearDtc() {
    // Отправляем запрос: ЭБУ=ECM, режим=0x04 (Clear DTCs).
    sendFrame(KLINE_ECU_ECM, 0x04, 0);
    // Ожидаем короткий ответ (>=2 байта) в качестве подтверждения.
    return (readFrame(50) >= 2);
}

/**
 * @brief Отправить запрос на сброс адаптации TCM.
 * 
 * Отправляет специальную последовательность для сброса адаптации коробки передач.
 */
void KLine::resetTcmAdaptation() {
    // Отправляем запрос: ЭБУ=TCM, режим=0x31 (поддерживаемый производителем), PID=0x0102.
    sendFrame(KLINE_ECU_TCM, 0x31, 0x0102);
    LOG_INFO(name, "TCM reset sent");
}

/**
 * @brief Быстрая инициализация (для диагностики протокола).
 * 
 * Упрощенная процедура инициализации, которая не включает в себя
 * ожидание ключевых байтов. Используется для быстрого определения,
 * поддерживает ли ЭБУ протокол K-Line.
 * 
 * @return Всегда false (не используется возвратное значение).
 */
bool KLine::fastInit() {
    // Отправка низкого уровня на 25 мс.
    pinMode(KLINE_TX, OUTPUT);
    digitalWrite(KLINE_TX, LOW);
    delay(25);
    // Возврат пина в режим INPUT_PULLUP.
    pinMode(KLINE_TX, INPUT_PULLUP);
    // Повторная инициализация UART.
    _uart->begin(10400, SERIAL_8N1, KLINE_RX, KLINE_TX);
    LOG_INFO(name, "Fast init done");
    return false;
}

// ============================================================================
// Симуляция
// ============================================================================

/**
 * @brief Генерация случайных данных для режима симуляции.
 * 
 * Заполняет структуру pack случайными, но реалистичными значениями.
 */
void KLine::simulateData() {
    pack.coolant_temp     = 85.0f + random(0, 20) / 10.0f;
    pack.atf_temp         = 70.0f + random(0, 15) / 10.0f;
    pack.voltage          = 14.0f + random(0, 50) / 100.0f;
    pack.fuel_percent     = 55.0f + random(0, 20) / 10.0f;
    pack.output_shaft_rpm = 1500.0f + random(0, 500);
}