import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../controllers/player_controller.dart';

Future<void> showAudioEffectsSheet({
  required BuildContext context,
  required PlayerController player,
}) {
  return Navigator.of(context).push<void>(
    MaterialPageRoute(builder: (_) => AudioEffectsPage(player: player)),
  );
}

class AudioEffectsPage extends StatefulWidget {
  const AudioEffectsPage({super.key, required this.player});

  final PlayerController player;

  @override
  State<AudioEffectsPage> createState() => _AudioEffectsPageState();
}

class _AudioEffectsPageState extends State<AudioEffectsPage> {
  var _tabIndex = 0;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final player = widget.player;

    return AnnotatedRegion<SystemUiOverlayStyle>(
      value: SystemUiOverlayStyle(
        statusBarColor: Colors.transparent,
        statusBarIconBrightness: colorScheme.brightness == Brightness.dark
            ? Brightness.light
            : Brightness.dark,
        statusBarBrightness: colorScheme.brightness == Brightness.dark
            ? Brightness.dark
            : Brightness.light,
      ),
      child: Scaffold(
        appBar: AppBar(
          leading: IconButton(
            tooltip: '关闭',
            onPressed: () => Navigator.of(context).pop(),
            icon: const Icon(Icons.close_rounded),
          ),
          title: const Text('自定义音效'),
          actions: [
            TextButton(
              onPressed: () => Navigator.of(context).pop(),
              child: const Text('保存'),
            ),
            const SizedBox(width: 8),
          ],
        ),
        body: AnimatedBuilder(
          animation: player,
          builder: (context, _) {
            if (!player.isAudioEffectsSupported) {
              return _UnsupportedView(colorScheme: colorScheme);
            }

            return Column(
              children: [
                _EffectTabs(index: _tabIndex, onChanged: _setTab),
                Expanded(
                  child: _tabIndex == 0
                      ? _EqualizerPanel(player: player)
                      : _EnhancePanel(player: player),
                ),
              ],
            );
          },
        ),
      ),
    );
  }

  void _setTab(int value) {
    setState(() => _tabIndex = value);
  }
}

class _EffectTabs extends StatelessWidget {
  const _EffectTabs({required this.index, required this.onChanged});

  final int index;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final labels = ['均衡器', '增强'];
    return Container(
      height: 86,
      color: colorScheme.surfaceContainer,
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
        children: [
          for (final entry in labels.indexed)
            InkWell(
              onTap: () => onChanged(entry.$1),
              child: SizedBox(
                width: 132,
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(
                      entry.$2,
                      style: Theme.of(context).textTheme.titleLarge?.copyWith(
                        color: entry.$1 == index
                            ? colorScheme.primary
                            : colorScheme.onSurface,
                        fontWeight: FontWeight.w900,
                      ),
                    ),
                    const SizedBox(height: 8),
                    AnimatedContainer(
                      duration: const Duration(milliseconds: 180),
                      width: entry.$1 == index ? 8 : 0,
                      height: 8,
                      decoration: BoxDecoration(
                        color: colorScheme.primary,
                        shape: BoxShape.circle,
                      ),
                    ),
                  ],
                ),
              ),
            ),
        ],
      ),
    );
  }
}

class _EqualizerPanel extends StatelessWidget {
  const _EqualizerPanel({required this.player});

  final PlayerController player;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final config = player.equalizerConfig;
    final minDb = config.minMillibels / 100;
    final maxDb = config.maxMillibels / 100;

    return Column(
      children: [
        SwitchListTile(
          contentPadding: const EdgeInsets.symmetric(horizontal: 22),
          title: const Text('启用均衡器'),
          subtitle: Text(
            player.equalizerEnabled ? player.equalizerPresetName : '关闭',
          ),
          value: player.equalizerEnabled,
          onChanged: player.setEqualizerEnabled,
        ),
        SizedBox(
          height: 84,
          child: CustomPaint(
            painter: _EqualizerCurvePainter(
              colorScheme: colorScheme,
              levels: player.equalizerLevels,
              min: config.minMillibels,
              max: config.maxMillibels,
            ),
            child: const SizedBox.expand(),
          ),
        ),
        Expanded(
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              SizedBox(
                width: 62,
                child: Padding(
                  padding: const EdgeInsets.only(top: 32, bottom: 58),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    crossAxisAlignment: CrossAxisAlignment.end,
                    children: [
                      Text('+${maxDb.round()}dB'),
                      const Text('0dB'),
                      Text('${minDb.round()}dB'),
                    ],
                  ),
                ),
              ),
              Expanded(
                child: SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  padding: const EdgeInsets.fromLTRB(4, 4, 22, 18),
                  child: Row(
                    children: [
                      for (
                        var index = 0;
                        index < config.bands.length &&
                            index < player.equalizerLevels.length;
                        index++
                      )
                        _EqualizerBandSlider(
                          label: _frequencyLabel(config.bands[index].centerHz),
                          value: player.equalizerLevels[index],
                          min: config.minMillibels,
                          max: config.maxMillibels,
                          enabled: player.equalizerEnabled,
                          onChanged: (value) => player.setEqualizerBandLevel(
                            index,
                            value,
                            persist: false,
                          ),
                          onChangeEnd: (value) =>
                              player.setEqualizerBandLevel(index, value),
                        ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(22, 8, 22, 28),
          child: Row(
            children: [
              Expanded(
                child: OutlinedButton(
                  onPressed: () => _showPresetPicker(context),
                  child: const Text('预设'),
                ),
              ),
              const SizedBox(width: 18),
              Expanded(
                child: OutlinedButton(
                  onPressed: player.resetEqualizer,
                  child: const Text('重置'),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Future<void> _showPresetPicker(BuildContext context) async {
    final preset = await showModalBottomSheet<AudioEffectPreset>(
      context: context,
      showDragHandle: true,
      builder: (sheetContext) {
        return Material(
          color: Theme.of(sheetContext).colorScheme.surface,
          child: SafeArea(
            top: false,
            child: ListView.separated(
              shrinkWrap: true,
              padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
              itemCount: PlayerController.equalizerPresets.length,
              separatorBuilder: (_, _) => const Divider(height: 1),
              itemBuilder: (context, index) {
                final preset = PlayerController.equalizerPresets[index];
                return ListTile(
                  leading: const Icon(Icons.tune_rounded),
                  title: Text(preset.name),
                  trailing: player.equalizerPresetName == preset.name
                      ? Icon(
                          Icons.check_rounded,
                          color: Theme.of(context).colorScheme.primary,
                        )
                      : null,
                  onTap: () => Navigator.of(sheetContext).pop(preset),
                );
              },
            ),
          ),
        );
      },
    );
    if (preset != null) {
      await player.applyEqualizerPreset(preset);
    }
  }
}

class _EqualizerBandSlider extends StatelessWidget {
  const _EqualizerBandSlider({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.enabled,
    required this.onChanged,
    required this.onChangeEnd,
  });

  final String label;
  final int value;
  final int min;
  final int max;
  final bool enabled;
  final ValueChanged<int> onChanged;
  final ValueChanged<int> onChangeEnd;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final dbValue = value / 100;
    return SizedBox(
      width: 72,
      child: Column(
        children: [
          SizedBox(
            height: 28,
            child: Text(
              dbValue == 0 ? '0' : dbValue.toStringAsFixed(1),
              style: Theme.of(
                context,
              ).textTheme.labelLarge?.copyWith(fontWeight: FontWeight.w800),
            ),
          ),
          Expanded(
            child: RotatedBox(
              quarterTurns: -1,
              child: SliderTheme(
                data: SliderTheme.of(context).copyWith(
                  trackHeight: 3,
                  thumbShape: const RoundSliderThumbShape(
                    enabledThumbRadius: 17,
                    disabledThumbRadius: 17,
                  ),
                  activeTrackColor: colorScheme.primary,
                  inactiveTrackColor: colorScheme.outlineVariant,
                ),
                child: Slider(
                  value: value.toDouble(),
                  min: min.toDouble(),
                  max: max.toDouble(),
                  onChanged: enabled ? (next) => onChanged(next.round()) : null,
                  onChangeEnd: enabled
                      ? (next) => onChangeEnd(next.round())
                      : null,
                ),
              ),
            ),
          ),
          const SizedBox(height: 10),
          Text(
            label,
            maxLines: 1,
            style: Theme.of(context).textTheme.titleMedium?.copyWith(
              color: colorScheme.primary,
              fontWeight: FontWeight.w900,
            ),
          ),
        ],
      ),
    );
  }
}

class _EnhancePanel extends StatelessWidget {
  const _EnhancePanel({required this.player});

  final PlayerController player;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final bassPercent = (player.bassBoostStrength * 100).round();

    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 28),
      children: [
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          secondary: Icon(Icons.speaker_rounded, color: colorScheme.primary),
          title: const Text('低音增强'),
          subtitle: Text(player.bassBoostEnabled ? 'Bass $bassPercent%' : '关闭'),
          value: player.bassBoostEnabled,
          onChanged: player.setBassBoostEnabled,
        ),
        const SizedBox(height: 18),
        Row(
          children: [
            const Text('Bass'),
            Expanded(
              child: Slider(
                value: player.bassBoostStrength,
                onChanged: player.bassBoostEnabled
                    ? (value) =>
                          player.setBassBoostStrength(value, persist: false)
                    : null,
                onChangeEnd: player.bassBoostEnabled
                    ? player.setBassBoostStrength
                    : null,
              ),
            ),
            SizedBox(
              width: 44,
              child: Text(
                '$bassPercent%',
                textAlign: TextAlign.end,
                style: Theme.of(
                  context,
                ).textTheme.bodyMedium?.copyWith(fontWeight: FontWeight.w800),
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _EqualizerCurvePainter extends CustomPainter {
  const _EqualizerCurvePainter({
    required this.colorScheme,
    required this.levels,
    required this.min,
    required this.max,
  });

  final ColorScheme colorScheme;
  final List<int> levels;
  final int min;
  final int max;

  @override
  void paint(Canvas canvas, Size size) {
    final gridPaint = Paint()
      ..color = colorScheme.outlineVariant.withValues(alpha: .56)
      ..strokeWidth = 1;
    for (var i = 1; i < 4; i++) {
      final y = size.height * i / 4;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), gridPaint);
    }

    final zeroY = _levelToY(0, size.height);
    final zeroPaint = Paint()
      ..color = colorScheme.primary.withValues(alpha: .62)
      ..strokeWidth = 2;
    canvas.drawLine(Offset(0, zeroY), Offset(size.width, zeroY), zeroPaint);

    if (levels.isEmpty) {
      return;
    }

    final linePaint = Paint()
      ..color = colorScheme.primary
      ..strokeWidth = 2.4
      ..style = PaintingStyle.stroke;
    final path = Path();
    for (var index = 0; index < levels.length; index++) {
      final x = levels.length == 1
          ? size.width / 2
          : size.width * index / (levels.length - 1);
      final y = _levelToY(levels[index], size.height);
      if (index == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(path, linePaint);
  }

  double _levelToY(int level, double height) {
    final span = (max - min).abs();
    if (span == 0) {
      return height / 2;
    }
    final normalized = ((level - min) / span).clamp(0.0, 1.0);
    return height * (1 - normalized);
  }

  @override
  bool shouldRepaint(covariant _EqualizerCurvePainter oldDelegate) {
    return oldDelegate.levels != levels ||
        oldDelegate.min != min ||
        oldDelegate.max != max ||
        oldDelegate.colorScheme != colorScheme;
  }
}

class _UnsupportedView extends StatelessWidget {
  const _UnsupportedView({required this.colorScheme});

  final ColorScheme colorScheme;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(28),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.info_outline_rounded,
              size: 42,
              color: colorScheme.primary,
            ),
            const SizedBox(height: 14),
            Text(
              '当前平台暂不支持音效调节',
              style: Theme.of(
                context,
              ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w900),
            ),
            const SizedBox(height: 8),
            Text(
              'Android 设备播放时可使用多段均衡器、预设和低音增强。',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                color: colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

String _frequencyLabel(int hz) {
  if (hz >= 1000) {
    final khz = hz / 1000;
    return khz == khz.roundToDouble()
        ? '${khz.round()}k'
        : '${khz.toStringAsFixed(1)}k';
  }
  return '$hz';
}
