#ifndef SPREADSHEET_LOG_H
#define SPREADSHEET_LOG_H

void spreadsheet_log_init();
void spreadsheet_log_loop();
// Call from the real-time counter task whenever it samples both PCNT channels.
void spreadsheet_log_capture_counts(int32_t ch1, int32_t ch2, unsigned long nowMs);

#endif // SPREADSHEET_LOG_H
