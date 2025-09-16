import { AbilityConstant, ConfigurationConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import { installFontConfigOnce } from '../utils/ConfigInstaller';

const DOMAIN = 0x0000;

export default class MainAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    this.context.getApplicationContext().setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_NOT_SET);
    hilog.info(DOMAIN, 'WebKitShell', '%{public}s', 'MainAbility onCreate');
  }

  onDestroy(): void {
    hilog.info(DOMAIN, 'WebKitShell', '%{public}s', 'MainAbility onDestroy');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    // Main window is created, set main page for this ability
    hilog.info(DOMAIN, 'WebKitShell', '%{public}s', 'MainAbility onWindowStageCreate');

    const fontconfPath = installFontConfigOnce(this.context);
    hilog.error(DOMAIN, 'WebKitShell', 'Fontconfig path: %{public}s', fontconfPath);

    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(DOMAIN, 'WebKitShell', 'Failed to load the content. Cause: %{public}s', JSON.stringify(err));
        return;
      }
      hilog.info(DOMAIN, 'WebKitShell', 'Succeeded in loading the content.');
    });
  }

  onWindowStageDestroy(): void {
    // Main window is destroyed, release UI related resources
    hilog.info(DOMAIN, 'WebKitShell', '%{public}s', 'MainAbility onWindowStageDestroy');
  }

  onForeground(): void {
    // Ability has brought to foreground
    hilog.info(DOMAIN, 'WebKitShell', '%{public}s', 'MainAbility onForeground');
  }

  onBackground(): void {
    // Ability has back to background
    hilog.info(DOMAIN, 'WebKitShell', '%{public}s', 'MainAbility onBackground');
  }
}
