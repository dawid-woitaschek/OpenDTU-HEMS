import { Tooltip } from 'bootstrap';

export const tooltip = {
    mounted(el: HTMLElement) {
        // Legacy icon hints also need to be reachable without a mouse.
        if (!el.matches('button, a[href], input, select, textarea, [tabindex]')) {
            el.setAttribute('tabindex', '0');
        }
        new Tooltip(el, { trigger: 'hover focus' });
    },
    updated(el: HTMLElement) {
        const title = el.getAttribute('title');
        if (title) {
            el.setAttribute('data-bs-original-title', title);
            el.removeAttribute('title');
            Tooltip.getInstance(el)?.setContent({ '.tooltip-inner': title });
        }
    },
    unmounted(el: HTMLElement) {
        Tooltip.getInstance(el)?.dispose();
    },
};
