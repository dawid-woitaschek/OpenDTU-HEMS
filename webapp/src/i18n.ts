import { createI18n } from 'vue-i18n';
import messages from '@intlify/unplugin-vue-i18n/messages';
import type { I18nOptions, IntlDateTimeFormat, IntlNumberFormat } from 'vue-i18n';
import { hemsSupplementMessages } from './hemsMessages';
import { hemsTariffMessages } from './hemsTariffMessages';
import { firmwareSupplementMessages } from './firmwareMessages';
import { profileMessages } from './profileMessages';
import { redesignMessages } from './redesignMessages';

export const allLocales = [
    { code: 'en', name: 'English' },
    { code: 'de', name: 'Deutsch' },
];

const dateTimeFormatsTemplate: IntlDateTimeFormat = {
    datetime: {
        hour: 'numeric',
        minute: 'numeric',
        second: 'numeric',
        year: 'numeric',
        month: 'numeric',
        day: 'numeric',
        hour12: false,
    },
};

const numberFormatTemplate: IntlNumberFormat = {
    decimal: {
        style: 'decimal',
    },
    decimalNoDigits: {
        style: 'decimal',
        minimumFractionDigits: 0,
        maximumFractionDigits: 0,
    },
    decimalOneDigit: {
        style: 'decimal',
        minimumFractionDigits: 1,
        maximumFractionDigits: 1,
    },
    decimalTwoDigits: {
        style: 'decimal',
        minimumFractionDigits: 2,
        maximumFractionDigits: 2,
    },
    percent: {
        style: 'percent',
    },
    percentOneDigit: {
        style: 'percent',
        minimumFractionDigits: 1,
        maximumFractionDigits: 1,
    },
    byte: {
        style: 'unit',
        unit: 'byte',
    },
    kilobyte: {
        style: 'unit',
        unit: 'kilobyte',
    },
    megabyte: {
        style: 'unit',
        unit: 'megabyte',
    },
    celsius: {
        style: 'unit',
        unit: 'celsius',
        maximumFractionDigits: 1,
    },
};

export const dateTimeFormats: I18nOptions['datetimeFormats'] = {};
export const numberFormats: I18nOptions['numberFormats'] = {};

allLocales.forEach((locale) => {
    dateTimeFormats[locale.code] = dateTimeFormatsTemplate;
    numberFormats[locale.code] = numberFormatTemplate;
});

export const i18n = createI18n({
    legacy: false,
    globalInjection: true,
    locale: supportedLocale(localStorage.getItem('locale') || navigator.language),
    fallbackLocale: allLocales[0]?.code ?? 'en',
    messages,
    datetimeFormats: dateTimeFormats,
    numberFormats: numberFormats,
});

for (const supplement of [
    hemsSupplementMessages,
    hemsTariffMessages,
    firmwareSupplementMessages,
    profileMessages,
    redesignMessages,
]) {
    Object.entries(supplement).forEach(([locale, messages]) => i18n.global.mergeLocaleMessage(locale, messages));
}

// Built-in EN/DE are the complete supported set. Device language packs cannot expand it.
localStorage.setItem('locale', i18n.global.locale.value);
document.documentElement.lang = i18n.global.locale.value;
export function supportedLocale(locale: string): 'en' | 'de' {
    return locale.toLowerCase().split('-')[0] === 'de' ? 'de' : 'en';
}
export function setLocale(locale: string) {
    const supported = supportedLocale(locale);
    i18n.global.locale.value = supported;
    localStorage.setItem('locale', supported);
    document.documentElement.lang = supported;
}
