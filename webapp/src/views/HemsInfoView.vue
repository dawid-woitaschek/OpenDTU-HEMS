<template>
    <BasePage :title="$t('hems.Info')" :isLoading="dataLoading" :show-reload="true" @reload="getStatus">
        <CardElement :text="$t('hems.Status')" textVariant="text-bg-primary" table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody v-if="!dataLoading">
                        <tr>
                            <th>{{ $t('hems.State') }}</th>
                            <td>{{ $t('hems.' + hemsStatus.state) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Decision') }}</th>
                            <td>{{ $t('hems.' + hemsStatus.decision) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.ExternalDevices') }}</th>
                            <td>{{ $t('hems.NoneConfigured') }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import type { HemsStatus } from '@/types/HemsStatus';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';

export default defineComponent({
    components: { BasePage, CardElement },
    data() {
        return { dataLoading: true, hemsStatus: {} as HemsStatus };
    },
    created() {
        this.getStatus();
    },
    methods: {
        getStatus() {
            this.dataLoading = true;
            fetch('/api/hems/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: HemsStatus) => {
                    this.hemsStatus = data;
                    this.dataLoading = false;
                });
        },
    },
});
</script>
