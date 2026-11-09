'use strict';

(() => {
  const $ = (id) => document.getElementById(id);
  const states = {
    flow: 'Поток', no_flow: 'Нет потока', unknown: 'Не определено',
    no_data: 'Нет данных', mode_1: 'Режим 1', mode_2: 'Режим 2',
  };
  const stateClasses = new Set(Object.keys(states));
  const numberFormat = new Intl.NumberFormat('ru-RU', { maximumFractionDigits: 3 });
  let settings = null;
  let charts = [];
  let thresholdCharts = [];
  let lastThresholds = null;
  let analysisController = null;
  let thresholdsController = null;
  let currentController = null;
  let eventsController = null;
  let historyBusy = false;
  let readsPaused = false;
  let currentTimer;
  let eventsTimer;

  function element(tag, text, className) {
    const node = document.createElement(tag);
    if (text !== undefined) node.textContent = String(text);
    if (className) node.className = className;
    return node;
  }

  function numeric(value) {
    return typeof value === 'number' && Number.isFinite(value);
  }

  function formatNumber(value) {
    return numeric(value) ? numberFormat.format(value) : '-';
  }

  function dateTime(value) {
    if (!numeric(value)) return '-';
    const date = new Date(value * 1000);
    return Number.isNaN(date.getTime()) ? '-' : date.toLocaleString('ru-RU');
  }

  function duration(value) {
    if (!numeric(value) || value < 0) return '-';
    if (value < 60) return `${formatNumber(value)} с`;
    const seconds = Math.round(value);
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor(seconds % 86400 / 3600);
    const minutes = Math.floor(seconds % 3600 / 60);
    return `${days ? `${days} д ` : ''}${hours ? `${hours} ч ` : ''}${minutes} мин ${seconds % 60} с`;
  }

  function stateName(state) {
    if (state === 'mode_1') return settings?.modes[0]?.name || states.mode_1;
    if (state === 'mode_2') return settings?.modes[1]?.name || states.mode_2;
    return states[state] || `Неизвестное состояние: ${String(state ?? '-')}`;
  }

  function status(id, text, error = false) {
    const node = $(id);
    node.textContent = text;
    node.classList.toggle('error', error);
  }

  async function request(url, options = {}) {
    const controller = new AbortController();
    const abort = () => controller.abort();
    if (options.signal?.aborted) controller.abort();
    options.signal?.addEventListener('abort', abort, { once: true });
    const timer = setTimeout(abort, 20000);
    try {
      const response = await fetch(url, { ...options, signal: controller.signal, cache: 'no-store' });
      if (!response.ok) {
        const detail = (await response.text()).slice(0, 400);
        throw new Error(`HTTP ${response.status}${detail ? `: ${detail}` : ''}`);
      }
      if (response.status === 204) return null;
      const text = await response.text();
      if (!text.trim()) return null;
      try { return JSON.parse(text); }
      catch { throw new Error('Сервер вернул некорректный JSON.'); }
    } catch (error) {
      if (error.name === 'AbortError' && !options.signal?.aborted) {
        throw new Error('Сервер не ответил за 20 секунд.');
      }
      throw error;
    } finally {
      clearTimeout(timer);
      options.signal?.removeEventListener('abort', abort);
    }
  }

  function table(headers, rows, caption) {
    const wrapper = element('div', undefined, 'table-scroll');
    const node = element('table');
    node.append(element('caption', caption));
    const head = element('thead');
    const headerRow = element('tr');
    headers.forEach((text) => {
      const cell = element('th', text);
      cell.scope = 'col';
      headerRow.append(cell);
    });
    head.append(headerRow);
    const body = element('tbody');
    rows.forEach((row) => {
      const tr = element('tr');
      row.forEach((text, index) => {
        const cell = element(index === 0 ? 'th' : 'td', text ?? '-');
        if (index === 0) cell.scope = 'row';
        tr.append(cell);
      });
      body.append(tr);
    });
    node.append(head, body);
    wrapper.append(node);
    return wrapper;
  }

  async function loadCurrent() {
    if (readsPaused || currentController) return;
    clearTimeout(currentTimer);
    const controller = new AbortController();
    currentController = controller;
    $('refresh-current').disabled = true;
    try {
      const data = await request('/api/current', { signal: controller.signal });
      if (controller !== currentController || controller.signal.aborted) return;
      if (!data || !Array.isArray(data.channels) || !data.connection) throw new Error('Неверный формат текущих данных.');
      const connection = data.connection;
      $('connection-state').textContent = connection.connected ? 'Приёмник подключён' : 'Приёмник отключён';
      $('connection-state').className = connection.connected ? 'connected' : 'error';
      $('connection-detail').textContent = `Порт: ${connection.port ?? '-'}; BEAT: ${dateTime(connection.last_beat)}${connection.error ? `; Ошибка: ${connection.error}` : ''}`;
      $('current-mode').textContent = stateName(data.mode);
      $('current-channels').replaceChildren(...data.channels.map((channel) => {
        const card = element('article', undefined, 'panel current-card');
        card.append(element('h3', `${channel.id}. ${channel.name ?? 'Без названия'}`));
        card.append(element('p', formatNumber(channel.value), 'raw-value'));
        const displayState = channel.available === false ? 'no_data' : channel.state;
        card.append(element('p', stateName(displayState), `state-label ${stateClasses.has(displayState) ? displayState : 'unknown'}`));
        card.append(element('p', `SEQ: ${numeric(channel.seq) ? channel.seq.toString(16).toUpperCase().padStart(2, '0') : '-'}`, 'hint'));
        card.append(element('p', `Последнее корректное: ${dateTime(channel.last_valid)}`, 'hint'));
        if (channel.error) card.append(element('p', `Ошибка датчика: ${channel.error}`, 'error'));
        return card;
      }));
      status('current-status', `Данные сервера: ${dateTime(data.time)}; получены: ${new Date().toLocaleTimeString('ru-RU')}`);
    } catch (error) {
      if (controller !== currentController || controller.signal.aborted) return;
      status('current-status', `Нет свежих данных: ${error.message} Последние показания, если есть, устарели.`, true);
    } finally {
      if (controller === currentController) {
        currentController = null;
        $('refresh-current').disabled = false;
        currentTimer = setTimeout(loadCurrent, 5000);
      }
    }
  }

  async function loadEvents() {
    if (readsPaused || eventsController) return;
    clearTimeout(eventsTimer);
    const controller = new AbortController();
    eventsController = controller;
    $('refresh-events').disabled = true;
    try {
      const events = await request('/api/events?limit=100', { signal: controller.signal });
      if (controller !== eventsController || controller.signal.aborted) return;
      if (!Array.isArray(events)) throw new Error('Неверный формат событий.');
      const rows = events.map((event) => [dateTime(event.time), event.kind, event.sensor, typeof event.detail === 'object' && event.detail !== null ? JSON.stringify(event.detail) : event.detail]);
      $('events-table').replaceChildren(events.length ? table(['Время', 'Тип', 'Датчик', 'Подробности'], rows, 'Диагностика приёмника и датчиков') : element('p', 'Событий нет.'));
      status('events-status', `Получено событий: ${events.length}; обновлено: ${new Date().toLocaleTimeString('ru-RU')}`);
    } catch (error) {
      if (controller !== eventsController || controller.signal.aborted) return;
      status('events-status', `Не удалось обновить события: ${error.message} Ранее загруженный список может быть устаревшим.`, true);
    } finally {
      if (controller === eventsController) {
        eventsController = null;
        $('refresh-events').disabled = false;
        eventsTimer = setTimeout(loadEvents, 15000);
      }
    }
  }

  function inputField(labelText, id, value, type = 'text', attributes = {}) {
    const label = element('label', labelText);
    const input = element('input');
    input.id = id;
    input.name = id;
    input.type = type;
    input.value = value ?? '';
    input.required = true;
    Object.entries(attributes).forEach(([name, value]) => input.setAttribute(name, value));
    label.append(input);
    return label;
  }

  function renderSettings() {
    $('port').value = settings.port;
    $('network-access').checked = settings.network_access;
    $('channel-settings').replaceChildren(...settings.channels.map((channel, index) => {
      const group = element('fieldset');
      group.append(element('legend', `Канал ${index + 1}`));
      group.append(inputField('Название', `channel-${index}-name`, channel.name));
      group.append(inputField('Назначение', `channel-${index}-role`, channel.role));
      // The role is an optional description, not a mode condition.
      group.querySelector(`#channel-${index}-role`).required = false;
      group.append(inputField('Порог T', `channel-${index}-threshold`, channel.threshold, 'number', { step: 'any' }));
      group.append(inputField('Гистерезис H (>= 0)', `channel-${index}-hysteresis`, channel.hysteresis, 'number', { step: 'any', min: '0' }));
      group.append(inputField('Тайм-аут, секунд (> 0)', `channel-${index}-timeout`, channel.timeout, 'number', { step: 'any', min: '0.001' }));
      return group;
    }));
    $('mode-settings').replaceChildren(...settings.modes.map((mode, index) => {
      const group = element('fieldset');
      group.append(element('legend', `Режим ${index + 1}`));
      group.append(inputField('Название режима', `mode-${index}-name`, mode.name));
      mode.conditions.forEach((condition, channelIndex) => {
        const label = element('label', `Канал ${channelIndex + 1}: ${settings.channels[channelIndex].name}`);
        const select = element('select');
        select.id = `mode-${index}-condition-${channelIndex}`;
        [['flow', 'Поток'], ['no_flow', 'Нет потока'], ['ignore', 'Не учитывать']].forEach(([value, text]) => {
          const option = element('option', text);
          option.value = value;
          select.append(option);
        });
        select.value = condition;
        select.required = true;
        label.append(select);
        group.append(label);
      });
      return group;
    }));
    $('settings-form').hidden = false;
  }

  async function loadSettings() {
    $('retry-settings').hidden = true;
    status('settings-status', 'Загрузка настроек...');
    try {
      const data = await request('/api/settings');
      if (!data || !Array.isArray(data.channels) || data.channels.length !== 3 || !Array.isArray(data.modes) || data.modes.length !== 2 || data.modes.some((mode) => !Array.isArray(mode.conditions) || mode.conditions.length !== 3)) {
        throw new Error('Ожидались настройки трёх каналов и двух режимов.');
      }
      settings = data;
      renderSettings();
      status('settings-status', 'Настройки загружены.');
      // Refresh the current mode names.
      loadCurrent();
      loadAnalysis();
    } catch (error) {
      status('settings-status', `Не удалось загрузить настройки: ${error.message}`, true);
      $('retry-settings').hidden = false;
    }
  }

  async function saveSettings(event) {
    event.preventDefault();
    if (!settings || !$('settings-form').reportValidity()) return;
    const next = {
      port: $('port').value.trim(),
      network_access: $('network-access').checked,
      channels: settings.channels.map((_, index) => ({
        name: $(`channel-${index}-name`).value.trim(),
        role: $(`channel-${index}-role`).value.trim(),
        threshold: Number($(`channel-${index}-threshold`).value),
        hysteresis: Number($(`channel-${index}-hysteresis`).value),
        timeout: Number($(`channel-${index}-timeout`).value),
      })),
      modes: settings.modes.map((_, index) => ({
        name: $(`mode-${index}-name`).value.trim(),
        conditions: settings.channels.map((_, channelIndex) => $(`mode-${index}-condition-${channelIndex}`).value),
      })),
    };
    if (!next.port || next.channels.some((channel) => !channel.name || !numeric(channel.threshold) || !numeric(channel.hysteresis) || channel.hysteresis < 0 || !numeric(channel.timeout) || channel.timeout <= 0) || next.modes.some((mode) => !mode.name)) {
      status('settings-status', 'Проверьте названия, порт и числовые параметры. Названия не могут состоять только из пробелов.', true);
      return;
    }
    $('settings-fields').disabled = true;
    $('save-settings').disabled = true;
    status('settings-status', 'Сохранение...');
    try {
      await request('/api/settings', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(next) });
      if (next.network_access !== settings.network_access) $('restart-notice').hidden = false;
      settings = next;
      status('settings-status', 'Настройки сохранены. Анализ пересчитывается.');
      renderSettings();
      loadCurrent();
      loadAnalysis();
    } catch (error) {
      status('settings-status', `Не удалось подтвердить сохранение: ${error.message} Проверьте соединение; введённые значения сохранены в форме.`, true);
    } finally {
      $('settings-fields').disabled = false;
      $('save-settings').disabled = false;
    }
  }

  function localInputValue(date) {
    const local = new Date(date.getTime() - date.getTimezoneOffset() * 60000);
    return local.toISOString().slice(0, 19);
  }

  function updatePeriodInputs() {
    const custom = $('period').value === 'custom';
    $('period-start').disabled = !custom;
    $('period-end').disabled = !custom;
    if (!custom) {
      const end = new Date();
      $('period-end').value = localInputValue(end);
      $('period-start').value = localInputValue(new Date(end.getTime() - Number($('period').value) * 1000));
    }
  }

  function updateTPeriodInputs() {
    const custom = $('tperiod').value === 'custom';
    $('tperiod-start').disabled = !custom;
    $('tperiod-end').disabled = !custom;
    if (!custom) {
      const end = new Date();
      $('tperiod-end').value = localInputValue(end);
      $('tperiod-start').value = localInputValue(new Date(end.getTime() - Number($('tperiod').value) * 1000));
    }
  }

  function timeline(intervals, start, end, label) {
    const container = element('div', undefined, 'timeline-block');
    const track = element('div', undefined, 'timeline');
    track.setAttribute('role', 'img');
    track.setAttribute('aria-label', `${label}: временная полоса. Точные интервалы доступны в списке ниже.`);
    const details = element('details', undefined, 'interval-details');
    const valid = (Array.isArray(intervals) ? intervals : []).filter((interval) => numeric(interval.start) && numeric(interval.end) && interval.end > interval.start && interval.end > start && interval.start < end);
    details.append(element('summary', `Интервалы: ${valid.length} (время и состояние)`));
    const rows = [];
    valid.forEach((interval) => {
      const left = Math.max(start, interval.start);
      const right = Math.min(end, interval.end);
      const text = `${stateName(interval.state)}; с ${dateTime(left)} до ${dateTime(right)}; ~ ${duration(right - left)}`;
      const segment = element('span', undefined, `segment ${stateClasses.has(interval.state) ? interval.state : 'unknown'}`);
      segment.style.left = `${(left - start) / (end - start) * 100}%`;
      segment.style.width = `${(right - left) / (end - start) * 100}%`;
      segment.title = text;
      track.append(segment);
      rows.push([stateName(interval.state), dateTime(left), dateTime(right), duration(right - left)]);
    });
    if (rows.length) details.append(table(['Состояние', 'Начало', 'Конец', '~ Длительность'], rows, label));
    else details.append(element('p', 'Интервалов нет.'));
    const axis = element('div', undefined, 'timeline-axis');
    axis.append(element('span', dateTime(start)), element('span', dateTime(end)));
    container.append(track, axis);
    if (!valid.length) container.append(element('p', 'Нет интервалов в выбранном периоде.', 'hint'));
    container.append(details);
    return container;
  }

  function drawChart(canvas, channel, start, end) {
    const measurements = Array.isArray(channel.measurements) ? channel.measurements : [];
    const points = measurements.filter((point) => numeric(point.time) && numeric(point.value)).map((point) => ({ x: point.time * 1000, y: point.value, seq: point.seq, state: point.state }));
    const datasets = [{
      label: 'Исходное значение', data: points, showLine: false,
      backgroundColor: '#185d9b', pointRadius: points.length > 2000 ? 1.5 : 2.5,
      pointHoverRadius: 5,
    }];
    if (numeric(channel.threshold) && numeric(channel.hysteresis)) {
      const half = channel.hysteresis / 2;
      [[channel.threshold + half, 'Включение: T + H/2', '#9d3d1c'], [channel.threshold - half, 'Выключение: T - H/2', '#4b6179']].forEach(([value, label, color]) => {
        datasets.push({ label: `${label} = ${formatNumber(value)}`, data: [{ x: start * 1000, y: value }, { x: end * 1000, y: value }], showLine: true, pointRadius: 0, borderWidth: 1.5, borderDash: [6, 4], borderColor: color });
      });
    }
    charts.push(new window.Chart(canvas, {
      type: 'scatter', data: { datasets },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        interaction: { mode: 'nearest', intersect: true },
        scales: {
          x: { type: 'linear', min: start * 1000, max: end * 1000, title: { display: true, text: 'Время получения группы (местное)' }, ticks: { maxTicksLimit: 7, callback: (value) => new Date(value).toLocaleString('ru-RU', { month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' }) } },
          y: { title: { display: true, text: 'Значение датчика' } },
        },
        plugins: {
          legend: { position: 'bottom' },
          tooltip: { callbacks: {
            title: (items) => items.length ? dateTime(items[0].parsed.x / 1000) : '',
            label: (context) => context.datasetIndex === 0 ? [`Значение: ${formatNumber(context.parsed.y)}`, `SEQ: ${context.raw.seq ?? '-'}`, stateName(context.raw.state)] : context.dataset.label,
          } },
        },
      },
    }));
  }

  function exportMeasurementsCsv(channel, measurements, start, end) {
    const stamp = (value) => new Date(value * 1000).toISOString().replace(/[:.]/g, '-');
    const rows = [['time', 'epoch', 'seq', 'value', 'state']];
    for (const measurement of measurements) {
      rows.push([new Date(measurement.time * 1000).toISOString(), measurement.time, measurement.seq ?? '', measurement.value, measurement.state]);
    }
    const csv = '\uFEFF' + rows.map((row) => row.join(',')).join('\r\n');
    const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' }));
    const link = document.createElement('a');
    link.href = url;
    link.download = `scs-channel-${channel.id}_${stamp(start)}_${stamp(end)}.csv`;
    document.body.append(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
  }

  function renderAnalysis(data) {
    if (!numeric(data.start) || !numeric(data.end) || data.end <= data.start || !Array.isArray(data.channels) || !data.mode || !data.mode.summary) throw new Error('Неверный формат анализа.');
    charts.forEach((chart) => chart.destroy());
    charts = [];
    $('channel-analysis').replaceChildren();
    $('analysis-results').hidden = false;
    $('analysis-period').textContent = `С ${dateTime(data.start)} до ${dateTime(data.end)}. Длительности приблизительны: время получения групп, не отдельных измерений.${data.approximate ? ' Сервер пометил анализ как приблизительный.' : ''}`;
    $('mode-1-legend').textContent = stateName('mode_1');
    $('mode-2-legend').textContent = stateName('mode_2');
    let chartFailed = !window.Chart;
    data.channels.forEach((channel) => {
      const panel = element('article', undefined, 'panel analysis-channel');
      const measurements = Array.isArray(channel.measurements) ? channel.measurements : [];
      const heading = element('div', undefined, 'section-heading');
      heading.append(element('h3', `${channel.id}. ${channel.name ?? 'Без названия'}`));
      const exportButton = element('button', 'Экспорт CSV');
      exportButton.type = 'button';
      exportButton.title = 'Скачать измерения канала за период в CSV';
      if (!measurements.length) exportButton.disabled = true;
      exportButton.addEventListener('click', () => exportMeasurementsCsv(channel, measurements, data.start, data.end));
      heading.append(exportButton);
      panel.append(heading);
      panel.append(element('p', `Назначение: ${channel.role || '-'}; T = ${formatNumber(channel.threshold)}; H = ${formatNumber(channel.hysteresis)}; тайм-аут: ${duration(channel.timeout)}`, 'hint'));
      if (window.Chart) {
        const chartBox = element('div', undefined, 'chart-box');
        const canvas = element('canvas');
        canvas.setAttribute('role', 'img');
        canvas.setAttribute('aria-label', `Исходные значения канала ${channel.name}, границы гистерезиса. Статистика приведена ниже.`);
        chartBox.append(canvas);
        panel.append(chartBox);
        // Chart.js requires the canvas to be in the document.
        $('channel-analysis').append(panel);
        try { drawChart(canvas, channel, data.start, data.end); }
        catch { chartFailed = true; chartBox.replaceChildren(element('p', 'Не удалось построить график.', 'error')); }
      }
      if (!measurements.length) panel.append(element('p', 'Измерений в этом периоде нет.', 'hint'));
      panel.append(timeline(channel.intervals, data.start, data.end, `Состояния канала ${channel.name}`));
      panel.append(table(['Выборка', 'Количество', 'Мин.', 'Макс.', 'Медиана'], ['all', 'flow', 'no_flow'].map((key) => {
        const stats = channel.values?.[key];
        return [key === 'all' ? 'Все измерения' : stateName(key), formatNumber(stats?.count), formatNumber(stats?.min), formatNumber(stats?.max), formatNumber(stats?.median)];
      }), 'Значения датчика'));
      panel.append(table(['Состояние', 'Интервалов', '~ Всего', '~ Мин.', '~ Макс.', '~ Медиана'], ['flow', 'no_flow', 'unknown'].map((key) => {
        const stats = channel.durations?.[key];
        return [stateName(key), formatNumber(stats?.count), duration(stats?.total), duration(stats?.min), duration(stats?.max), duration(stats?.median)];
      }), 'Приблизительные длительности состояний'));
      $('channel-analysis').append(panel);
    });
    $('chart-status').hidden = !chartFailed;
    $('chart-status').textContent = window.Chart ? 'Некоторые графики не удалось построить. Временные полосы и статистика доступны ниже.' : 'Локальная библиотека Chart.js не загружена. Проверьте файлы сервиса или блокировку скриптов и перезагрузите страницу. Графики недоступны; временные полосы и таблицы работают.';
    $('mode-timeline').replaceChildren(timeline(data.mode.intervals, data.start, data.end, 'Интервалы режимов'));
    $('mode-switches').textContent = `Переключений: ${formatNumber(data.mode.switches)}`;
    $('mode-statistics').replaceChildren(table(['Режим', '~ Длительность', 'Доля периода', 'Интервалов', '~ Мин.', '~ Макс.', '~ Медиана'], ['mode_1', 'mode_2', 'unknown', 'no_data'].map((key) => {
      const stats = data.mode.summary[key];
      return [stateName(key), duration(stats?.duration), numeric(stats?.share) ? `${formatNumber(stats.share * 100)} %` : '-', formatNumber(stats?.count), duration(stats?.min), duration(stats?.max), duration(stats?.median)];
    }), 'Режимы: приблизительные длительности; доля от всего периода'));
  }

  async function loadAnalysis(event) {
    event?.preventDefault();
    if (readsPaused) return;
    updatePeriodInputs();
    const start = new Date($('period-start').value).getTime() / 1000;
    const end = new Date($('period-end').value).getTime() / 1000;
    if (!numeric(start) || !numeric(end) || end <= start) {
      status('analysis-status', 'Укажите корректный период: конец должен быть позже начала.', true);
      return;
    }
    analysisController?.abort();
    const controller = new AbortController();
    analysisController = controller;
    $('load-analysis').disabled = true;
    $('analysis-results').hidden = true;
    status('analysis-status', 'Загрузка и расчёт анализа...');
    try {
      const data = await request(`/api/analysis?start=${Math.floor(start)}&end=${Math.floor(end)}`, { signal: controller.signal });
      if (controller !== analysisController || controller.signal.aborted) return;
      if (!data) throw new Error('Сервер вернул пустой анализ.');
      renderAnalysis(data);
      status('analysis-status', `Анализ загружен: ${new Date().toLocaleTimeString('ru-RU')}.`);
    } catch (error) {
      if (controller.signal.aborted) return;
      $('analysis-results').hidden = true;
      status('analysis-status', `Не удалось получить анализ: ${error.message}`, true);
    } finally {
      if (controller === analysisController) $('load-analysis').disabled = false;
    }
  }

  function drawHistogram(canvas, channel) {
    const logarithmic = $('taxis').value === 'logarithmic';
    const bins = channel.bins || [];
    const points = bins.map((bin) => ({ x: (bin.start + bin.end) / 2, y: bin.count }));
    const maxCount = Math.max(1, ...points.map((point) => point.y));
    const top = maxCount * 1.1;
    const datasets = [{
      type: 'bar', label: 'Измерения', data: points,
      backgroundColor: '#185d9b', barPercentage: 1, categoryPercentage: 1,
    }];
    const threshold = channel.threshold;
    if (threshold) {
      // A log axis cannot map 0, so the lines start at the axis minimum there.
      const bottom = logarithmic ? 1 : 0;
      // Filled rectangle between the band edges; drawn first so the lines stay on top.
      datasets.push({
        type: 'line', label: 'Полоса гистерезиса',
        data: [{ x: threshold.turn_off, y: top }, { x: threshold.turn_on, y: top }],
        pointRadius: 0, borderWidth: 0, fill: 'origin', backgroundColor: 'rgba(181, 122, 25, 0.25)',
      });
      [[threshold.t, `T = ${formatNumber(threshold.t)}`, '#9d3d1c', [6, 4]],
       [threshold.turn_off, `Выключение: ${formatNumber(threshold.turn_off)}`, '#b57a19', []],
       [threshold.turn_on, `Включение: ${formatNumber(threshold.turn_on)}`, '#b57a19', []]].forEach(([x, label, color, dash]) => {
        datasets.push({ type: 'line', label, data: [{ x, y: bottom }, { x, y: top }], pointRadius: 0, borderWidth: 1.5, borderDash: dash, borderColor: color });
      });
    }
    thresholdCharts.push(new window.Chart(canvas, {
      type: 'bar', data: { datasets },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        scales: {
          x: { type: 'linear', min: bins[0]?.start, max: bins[bins.length - 1]?.end, title: { display: true, text: 'Значение датчика' } },
          // Log scale: min 1 keeps empty (0) bins invisible instead of breaking the axis.
          y: logarithmic
            ? { type: 'logarithmic', min: 1, max: top, ticks: { maxTicksLimit: 6 }, title: { display: true, text: 'Число измерений (лог)' } }
            : { beginAtZero: true, max: top, title: { display: true, text: 'Число измерений' } },
        },
        plugins: { legend: { position: 'bottom' } },
      },
    }));
  }

  function renderThresholds(data) {
    if (!numeric(data.start) || !numeric(data.end) || data.end <= data.start || !Array.isArray(data.channels) || data.channels.length !== 3) throw new Error('Неверный формат калибровки.');
    lastThresholds = data;
    thresholdCharts.forEach((chart) => chart.destroy());
    thresholdCharts = [];
    $('channel-thresholds').replaceChildren();
    $('thresholds-results').hidden = false;
    $('thresholds-period').textContent = `Период: с ${dateTime(data.start)} до ${dateTime(data.end)}.`;
    let chartFailed = !window.Chart;
    data.channels.forEach((channel) => {
      const panel = element('article', undefined, 'panel analysis-channel');
      panel.append(element('h3', `${channel.id}. ${channel.name ?? 'Без названия'}`));
      panel.append(element('p', `Измерений: ${formatNumber(channel.count)}; мин: ${formatNumber(channel.min)}; макс: ${formatNumber(channel.max)}; медиана: ${formatNumber(channel.median)}`, 'hint'));
      let chartBox = null;
      let canvas = null;
      if (window.Chart && channel.bins) {
        chartBox = element('div', undefined, 'chart-box');
        canvas = element('canvas');
        canvas.setAttribute('role', 'img');
        canvas.setAttribute('aria-label', `Гистограмма значений канала ${channel.name} и рекомендуемый порог.`);
        chartBox.append(canvas);
        panel.append(chartBox);
      }
      if (!channel.bins) panel.append(element('p', `Гистограмма недоступна: ${channel.reason ?? 'нет данных.'}`, 'hint'));
      const threshold = channel.threshold;
      panel.append(table(['Параметр', 'Значение'], threshold ? [
        ['Рекомендуемый порог T', formatNumber(threshold.t)],
        ['Гистерезис H', formatNumber(threshold.h)],
        ['Включение, строго выше', formatNumber(threshold.turn_on)],
        ['Выключение, строго ниже', formatNumber(threshold.turn_off)],
      ] : [
        ['Рекомендация', channel.reason ?? 'нет'],
      ], 'Рекомендация порога; не сохраняется в настройки автоматически'));
      $('channel-thresholds').append(panel);
      // Chart.js requires the canvas to be in the document.
      if (canvas) {
        try { drawHistogram(canvas, channel); }
        catch { chartFailed = true; chartBox.replaceChildren(element('p', 'Не удалось построить гистограмму.', 'error')); }
      }
    });
    $('thresholds-chart-status').hidden = !chartFailed;
    $('thresholds-chart-status').textContent = 'Некоторые гистограммы не удалось построить. Таблицы рекомендаций доступны ниже.';
  }

  async function loadThresholds(event) {
    event?.preventDefault();
    if (readsPaused) return;
    updateTPeriodInputs();
    const start = new Date($('tperiod-start').value).getTime() / 1000;
    const end = new Date($('tperiod-end').value).getTime() / 1000;
    if (!numeric(start) || !numeric(end) || end <= start) {
      status('thresholds-status', 'Укажите корректный период: конец должен быть позже начала.', true);
      return;
    }
    thresholdsController?.abort();
    const controller = new AbortController();
    thresholdsController = controller;
    $('load-thresholds').disabled = true;
    $('thresholds-results').hidden = true;
    status('thresholds-status', 'Загрузка и расчёт...');
    try {
      const data = await request(`/api/thresholds?start=${Math.floor(start)}&end=${Math.floor(end)}`, { signal: controller.signal });
      if (controller !== thresholdsController || controller.signal.aborted) return;
      if (!data) throw new Error('Сервер вернул пустой результат.');
      renderThresholds(data);
      status('thresholds-status', `Рассчитано: ${new Date().toLocaleTimeString('ru-RU')}.`);
    } catch (error) {
      if (controller.signal.aborted) return;
      $('thresholds-results').hidden = true;
      status('thresholds-status', `Не удалось рассчитать пороги: ${error.message}`, true);
    } finally {
      if (controller === thresholdsController) $('load-thresholds').disabled = false;
    }
  }

  function onTaxisChange() {
    if (lastThresholds) renderThresholds(lastThresholds);
  }

  function updateHistoryInputs() {
    const period = $('history-scope').value === 'period';
    $('history-start').disabled = !period;
    $('history-end').disabled = !period;
  }

  async function deleteHistory(event) {
    event.preventDefault();
    if (historyBusy || !$('history-form').reportValidity()) return;
    const scope = $('history-scope').value;
    const body = { scope, confirm: true };
    let description = 'ВСЮ историю';
    if (scope === 'period') {
      body.start = new Date($('history-start').value).getTime() / 1000;
      body.end = new Date($('history-end').value).getTime() / 1000;
      if (!numeric(body.start) || !numeric(body.end) || body.end <= body.start) {
        status('history-status', 'Укажите корректный период: конец должен быть позже начала.', true);
        return;
      }
      description = `историю за период [${dateTime(body.start)}, ${dateTime(body.end)}) в часовом поясе браузера (начало включительно, конец не включительно)`;
    } else if (scope !== 'all') {
      status('history-status', 'Выберите область удаления.', true);
      return;
    }
    if (!window.confirm(`Удалить ${description}?\n\nБудут удалены измерения (группы целиком по времени получения) и диагностические события. Настройки сохранятся. Удаление необратимо. Автоматическая резервная копия не создаётся.`)) return;

    historyBusy = true;
    $('history-fields').disabled = true;
    readsPaused = true;
    clearTimeout(currentTimer);
    clearTimeout(eventsTimer);
    currentController?.abort();
    eventsController?.abort();
    analysisController?.abort();
    thresholdsController?.abort();
    // Invalidate even responses already received before the abort.
    currentController = null;
    eventsController = null;
    analysisController = null;
    thresholdsController = null;
    $('refresh-current').disabled = true;
    $('refresh-events').disabled = true;
    $('load-analysis').disabled = true;
    $('load-thresholds').disabled = true;
    lastThresholds = null;
    // Do not leave deleted data visible if a subsequent refresh fails or its period is invalid.
    $('current-channels').replaceChildren();
    $('current-mode').textContent = '-';
    $('events-table').replaceChildren();
    charts.forEach((chart) => chart.destroy());
    charts = [];
    thresholdCharts.forEach((chart) => chart.destroy());
    thresholdCharts = [];
    $('analysis-results').hidden = true;
    $('channel-analysis').replaceChildren();
    $('thresholds-results').hidden = true;
    $('channel-thresholds').replaceChildren();
    $('thresholds-chart-status').hidden = true;
    status('thresholds-status', 'История изменена. Рассчитайте пороги заново.');
    status('current-status', 'Обновление данных после операции удаления...');
    status('events-status', 'Обновление событий после операции удаления...');
    status('history-status', 'Удаление истории...');
    try {
      const data = await request('/api/history', {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const deleted = data?.deleted;
      if (!deleted || !['groups', 'records', 'events'].every((key) => Number.isInteger(deleted[key]) && deleted[key] >= 0)) {
        throw new Error('Неверный формат результата удаления.');
      }
      status('history-status', `Удалено групп: ${deleted.groups}, записей: ${deleted.records}, событий: ${deleted.events}. Настройки сохранены.`);
    } catch (error) {
      status('history-status', `Не удалось подтвердить удаление: ${error.message} Данные будут запрошены заново; результат удаления неизвестен.`, true);
    } finally {
      readsPaused = false;
      $('load-analysis').disabled = false;
      $('load-thresholds').disabled = false;
      // Refresh after errors too: the server may have committed before a connection failure.
      await Promise.all([loadCurrent(), loadAnalysis(), loadEvents()]);
      $('history-fields').disabled = false;
      historyBusy = false;
    }
  }

  $('history-scope').addEventListener('change', updateHistoryInputs);
  $('history-form').addEventListener('submit', deleteHistory);
  updateHistoryInputs();
  $('timezone').textContent = Intl.DateTimeFormat().resolvedOptions().timeZone;
  $('period').addEventListener('change', updatePeriodInputs);
  $('period-form').addEventListener('submit', loadAnalysis);
  $('tperiod').addEventListener('change', updateTPeriodInputs);
  $('taxis').addEventListener('change', onTaxisChange);
  $('thresholds-form').addEventListener('submit', loadThresholds);
  $('settings-form').addEventListener('submit', saveSettings);
  $('retry-settings').addEventListener('click', loadSettings);
  $('refresh-current').addEventListener('click', loadCurrent);
  $('refresh-events').addEventListener('click', loadEvents);
  if (!window.Chart) {
    $('chart-status').hidden = false;
    $('chart-status').textContent = 'Локальная библиотека Chart.js не загружена. Графики недоступны. Проверьте файлы сервиса или блокировку скриптов и перезагрузите страницу; остальные функции работают.';
  }
  updatePeriodInputs();
  updateTPeriodInputs();
  loadCurrent();
  loadEvents();
  loadSettings();
})();
