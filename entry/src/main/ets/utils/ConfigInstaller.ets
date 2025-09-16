import type common from '@ohos.app.ability.common';
import { buffer } from '@kit.ArkTS';
import fs from '@ohos.file.fs';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN = 0x0000;

export async function installFontConfigOnce(ctx: common.UIAbilityContext): Promise<string> {
  const appCtx = ctx.getApplicationContext();
  const dir = `${appCtx.filesDir}/fontconfig`;
  const dest = `${dir}/fonts.conf`;

  try {
    if (fs.accessSync(dest)) {
      hilog.info(DOMAIN, 'WebKitShell', 'Fontconfig already exists at %{public}s', dest);
      return dest;
    }
    fs.mkdirSync(dir);
    const uint8Array: Uint8Array = ctx.resourceManager.getRawFileContentSync('fonts.conf');
    const buf = buffer.from(uint8Array).buffer;
    const file = fs.openSync(dest, fs.OpenMode.WRITE_ONLY | fs.OpenMode.CREATE);
    if (file.fd) {
      fs.writeSync(file.fd, buf);
      fs.closeSync(file);
    }
  } catch (e) {
    hilog.info(DOMAIN, 'WebKitShell', 'Copy fonts.conf failed: %{public}s', JSON.stringify(e));
  }

  return dest;
}

