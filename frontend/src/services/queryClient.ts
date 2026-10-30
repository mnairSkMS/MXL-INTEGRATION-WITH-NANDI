import { QueryClient } from '@tanstack/react-query';

export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: false, // Do not auto-retry destructive or status queries
      staleTime: 5000,
    },
    mutations: {
      retry: false, // Per prompt: do not automatically retry save/restart operations
    },
  },
});
