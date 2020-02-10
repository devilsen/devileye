package me.devilsen.devileye.thread;

import me.devilsen.devileye.code.CodeResult;

/**
 * desc :
 * date : 2019-07-02 20:39
 *
 * @author : dongSen
 */
public interface Callback {

    void onDecodeComplete(CodeResult result);

    void onDarkBrightness(boolean isDark);

}
