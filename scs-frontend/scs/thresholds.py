"""Рекомендация порога T и гистерезиса H по гистограмме значений за период.

Правила (без внешних зависимостей):
- Гистограмма из 50 равных корзин на [0, максимум], сглаживание окном из 3 корзин.
- Два главных кластера — два наиболее высоких сглаженных пика на расстоянии не меньше
  четверти диапазона (плечо широкого распределения не считается вторым кластером).
  Пик в крайних корзинах тоже учитывается (типичный случай — значения "нет потока" ровно 0).
- Каждый кластер должен содержать не меньше SIDE_SHARE всех измерений.
- T ставится в зону минимальной плотности между пиками: корзина с наименьшим сглаженным
  счётом, зона расширяется в обе стороны, пока плотность не превысит максимум
  (1, VALLEY_LIMIT от пика). Зона покрывается полосой гистерезиса
  [T - H/2, T + H/2] (ширина не меньше двух корзин): включение строго выше полосы,
  выключение строго ниже. Переходные значения при переключении клапана остаются
  внутри полосы и не переключают состояние.
Рекомендация не сохраняется в настройках автоматически.
"""

from statistics import median

BINS = 50
MIN_SAMPLES = 20
PEAK_SHARE = 0.01
SIDE_SHARE = 0.03
VALLEY_LIMIT = 0.05


def _smoothed(counts):
    return [sum(window) / len(window) for window in (counts[max(0, i - 1):i + 2]
                                                     for i in range(len(counts)))]


def _main_peaks(counts, smoothed):
    total = sum(counts)
    candidates = []
    for i, count in enumerate(counts):
        if count < PEAK_SHARE * total:
            continue
        if i == 0:
            is_peak = smoothed[0] >= smoothed[1]
        elif i == len(counts) - 1:
            is_peak = smoothed[i] >= smoothed[i - 1]
        else:
            left, right = i, i
            while left > 0 and smoothed[left - 1] == smoothed[i]:
                left -= 1
            while right < len(counts) - 1 and smoothed[right + 1] == smoothed[i]:
                right += 1
            is_peak = (left == 0 or smoothed[i] > smoothed[left - 1]) and (
                right == len(counts) - 1 or smoothed[i] > smoothed[right + 1])
        if is_peak:
            candidates.append(i)
    candidates.sort(key=lambda i: -smoothed[i])
    peaks = []
    for i in candidates:
        if all(abs(i - peak) >= BINS // 4 for peak in peaks):
            peaks.append(i)
        if len(peaks) == 2:
            break
    return sorted(peaks)


def recommend_threshold(values):
    values = list(values)
    total = len(values)
    base = dict(count=total,
                min=min(values) if values else None,
                max=max(values) if values else None,
                median=median(values) if values else None)
    if total < MIN_SAMPLES:
        return {**base, "bins": None, "threshold": None,
                "reason": f"Мало измерений: {total}, нужно не меньше {MIN_SAMPLES}."}
    highest = max(values)
    if highest <= 0:
        return {**base, "bins": None, "threshold": None, "reason": "Все значения не больше нуля."}
    width = highest / BINS
    counts = [0] * BINS
    for value in values:
        counts[min(int(value / width), BINS - 1)] += 1
    bins = [dict(start=round(i * width, 3), end=round((i + 1) * width, 3), count=counts[i])
            for i in range(BINS)]
    smoothed = _smoothed(counts)
    peaks = _main_peaks(counts, smoothed)
    if len(peaks) < 2:
        return {**base, "bins": bins, "threshold": None,
                "reason": "Не удалось найти два кластера значений."}
    low_peak, high_peak = peaks
    mid = (low_peak + high_peak + 1) // 2
    if min(sum(counts[:mid]), sum(counts[mid:])) < SIDE_SHARE * total:
        return {**base, "bins": bins, "threshold": None,
                "reason": "Один из кластеров слишком мал для калибровки."}
    limit = max(1.0, VALLEY_LIMIT * max(smoothed))
    index = min(range(low_peak + 1, high_peak), key=lambda i: (smoothed[i], i))
    lo, hi = index, index
    while lo > low_peak and smoothed[lo - 1] <= limit:
        lo -= 1
    while hi < high_peak and smoothed[hi + 1] <= limit:
        hi += 1
    while hi - lo + 1 < 2:
        if hi < high_peak:
            hi += 1
        else:
            lo -= 1
    if lo == 0:
        lo = 1
    if hi == BINS - 1:
        hi = BINS - 2
    low, high = lo * width, (hi + 1) * width
    t = (low + high) / 2
    h = high - low
    return {**base, "bins": bins, "threshold": dict(
        t=round(t, 3), h=round(h, 3),
        turn_on=round(t + h / 2, 3), turn_off=round(t - h / 2, 3)), "reason": None}
