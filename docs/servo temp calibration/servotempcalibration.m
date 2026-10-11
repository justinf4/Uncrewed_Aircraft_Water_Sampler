% Servo Stop Dead Band Analysis
% Data: [temperature, low, high] per trial
clear;clc;close all;
trials = [
    23.5, 1479, 1503;
    38.5, 1406, 1426;
    39.5, 1400, 1424;
    41, 1396, 1416;
    41.5, 1386, 1411;
    42, 1385, 1406;
];

temps   = trials(:,1);
lo      = trials(:,2);
hi      = trials(:,3);
mid     = (lo + hi) / 2;
spread  = hi - lo;
avgSpread = mean(spread);

% Linear regression on midpoints
p = polyfit(temps, mid, 1);
fprintf('Linear fit: stop = %.2fx + %.1f\n', p(1), p(2));

% R-squared
mid_pred = polyval(p, temps);
ss_res = sum((mid - mid_pred).^2);
ss_tot = sum((mid - mean(mid)).^2);
r2 = 1 - ss_res/ss_tot;
fprintf('R² = %.4f\n', r2);

% Average dead band width
fprintf('Avg dead band width = %.1f us\n', avgSpread);

% Fit line range
t_fit = linspace(20, 45, 200);
mid_fit = polyval(p, t_fit);
hi_fit  = mid_fit + avgSpread/2;
lo_fit  = mid_fit - avgSpread/2;

% ── Plot ──────────────────────────────────────
figure('Color','white','Position',[100 100 800 500]);
hold on;

% Dead band shaded region
fill([t_fit, fliplr(t_fit)], [hi_fit, fliplr(lo_fit)], ...
    [0.216 0.541 0.867], 'FaceAlpha', 0.15, 'EdgeColor', 'none', ...
    'HandleVisibility', 'off');

% Dead band hi/lo points
plot(temps, hi, '^', 'Color', [0.216 0.541 0.867], ...
    'MarkerFaceColor', [0.216 0.541 0.867], 'MarkerSize', 8, ...
    'DisplayName', 'Dead band high');
plot(temps, lo, 'v', 'Color', [0.216 0.541 0.867], ...
    'MarkerFaceColor', [0.216 0.541 0.867], 'MarkerSize', 8, ...
    'DisplayName', 'Dead band low');

% Vertical lines connecting lo to hi per trial
for i = 1:length(temps)
    plot([temps(i) temps(i)], [lo(i) hi(i)], '-', ...
        'Color', [0.216 0.541 0.867 0.4], 'LineWidth', 1.5, ...
        'HandleVisibility', 'off');
end

% Midpoints
plot(temps, mid, 'o', 'Color', [0.886 0.294 0.290], ...
    'MarkerFaceColor', [0.886 0.294 0.290], 'MarkerSize', 9, ...
    'DisplayName', 'Midpoint');

% Linear fit line
plot(t_fit, mid_fit, '-', 'Color', [0.886 0.294 0.290], ...
    'LineWidth', 2.5, 'DisplayName', ...
    sprintf('Linear fit: y = %.2fx + %.0f  (R²=%.4f)', p(1), p(2), r2));

% Dead band bound lines
plot(t_fit, hi_fit, '--', 'Color', [0.216 0.541 0.867], ...
    'LineWidth', 1.2, 'HandleVisibility', 'off');
plot(t_fit, lo_fit, '--', 'Color', [0.216 0.541 0.867], ...
    'LineWidth', 1.2, 'HandleVisibility', 'off');

hold off;

xlabel('Temperature (°C)', 'FontSize', 14);
ylabel('Stop value (µs)', 'FontSize', 14);
title('PCA9685 Driver Board: Stop Dead Band on HSR-2648CR Servos vs Temperature', 'FontSize', 18);
legend('Location', 'northeast', 'FontSize', 12);
xlim([20 45]);
ylim([1350 1530]);
grid on;
ax = gca;
ax.GridAlpha = 0.15;
ax.Box = 'on';
ax.FontSize = 12;