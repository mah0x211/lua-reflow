-- Benchmark data of each scenario, ported from bench/data/*.js of the
-- JavaScript version. Date.now() is replaced by the fixed time NOW so that
-- every run renders the same output. JavaScript null is cjson.null, which
-- rendering takes as null: the templates compare those fields with null
-- strictly, so an absent field would not equal it.
local NULL = require('cjson').null
local floor = math.floor
local format = string.format
local sin = math.sin

local NOW = 1794410860117

--- @param list any[]
--- @param i integer zero-based index, as in the JavaScript generators
--- @return any
local function pick(list, i)
    return list[i % #list + 1]
end

--- @param n number
--- @param digits integer
--- @return number
local function to_fixed(n, digits)
    return tonumber(format('%.' .. digits .. 'f', n))
end

--- @param n integer
--- @param fn fun(i: integer): table
--- @return table[]
local function generate(n, fn)
    local list = {}
    for i = 0, n - 1 do
        list[i + 1] = fn(i)
    end
    return list
end

local function services_list()
    local statuses = {
        'healthy',
        'degraded',
        'down',
        'maintenance',
    }
    local regions = {
        'us-east-1',
        'eu-west-1',
        'ap-northeast-1',
    }
    local types = {
        'api',
        'database',
        'cache',
        'queue',
        'cdn',
    }
    return {
        org = {
            name = 'Acme Corp',
            plan = 'enterprise',
            memberCount = 142,
        },
        services = generate(40, function(i)
            return {
                id = format('svc-%03d', i + 1),
                name = format('%s-service-%d', pick(types, i), i + 1),
                type = pick(types, i),
                status = pick(statuses, i),
                region = pick(regions, i),
                uptime = to_fixed(99 - (i % 5) * 0.3, 2),
                responseTimeMs = 120 + (i % 10) * 30,
                requestsPerMin = 1000 + i * 47,
                errorRate = to_fixed(0.01 + (i % 4) * 0.005, 3),
                lastIncident = i % 3 == 0 and NOW - i * 60000 or NULL,
                tags = {
                    format('team-%d', i % 6),
                    'env-prod',
                    pick(types, i),
                },
                deployed = NOW - i * 86400000,
            }
        end),
        summaryStats = {
            total = 40,
            healthy = 20,
            degraded = 10,
            down = 5,
            maintenance = 5,
        },
    }
end

local function alert_feed()
    local severities = {
        'critical',
        'high',
        'medium',
        'low',
        'info',
    }
    local channels = {
        'pagerduty',
        'slack',
        'email',
        'webhook',
    }
    local titles = {
        'CPU high',
        'Memory pressure',
        'Latency spike',
        'Error surge',
        '5xx rate',
    }
    return {
        activeCount = 23,
        alerts = generate(60, function(i)
            return {
                id = format('alert-%d', i + 1),
                title = format('Alert: %s on svc-%03d', pick(titles, i),
                               i % 40 + 1),
                severity = pick(severities, i),
                acknowledged = i % 3 ~= 0,
                channel = pick(channels, i),
                firedAt = NOW - i * 5 * 60000,
                resolvedAt = i % 4 == 0 and NOW - i * 60000 or NULL,
                notes = i % 5 == 0 and
                    'Auto-remediation attempted. Manual review required.' or
                    NULL,
            }
        end),
    }
end

local function user_management()
    local roles = {
        'owner',
        'admin',
        'member',
        'viewer',
        'billing',
    }
    local plans = {
        'enterprise',
        'pro',
        'starter',
    }
    local names = {
        'Alice Chen',
        'Bob Smith',
        'Carol Jones',
        'David Kim',
        'Eve Nakamura',
    }
    return {
        pagination = {
            page = 1,
            perPage = 25,
            total = 142,
        },
        users = generate(25, function(i)
            return {
                id = format('usr-%04d', i + 1),
                name = format('%s #%d', pick(names, i), i + 1),
                email = format('user%d@acme.example.com', i + 1),
                role = pick(roles, i),
                plan = pick(plans, i),
                mfaEnabled = i % 2 == 0,
                lastLoginAt = NOW - i * 3600000,
                apiKeyCount = i % 4,
                invitePending = i % 7 == 0,
            }
        end),
    }
end

local function metrics_overview()
    local paths = {
        'users',
        'services',
        'alerts',
        'metrics',
        'billing',
    }
    local methods = {
        'GET',
        'POST',
        'PUT',
        'DELETE',
    }
    return {
        window = '24h',
        totalRequests = 1920000,
        totalErrors = 3840,
        errorRatePct = 0.2,
        avgResponseMs = 52,
        p95ResponseMs = 145,
        topEndpoints = generate(10, function(i)
            return {
                path = '/api/v1/' .. pick(paths, i),
                method = pick(methods, i),
                count = 120000 - i * 10000,
                avgMs = 30 + i * 8,
                errorPct = i * 0.05,
            }
        end),
        timeSeries = generate(24, function(i)
            return {
                hour = i,
                -- Math.round rounds halves up, as floor(x + 0.5) does.
                requests = 80000 + floor(sin(i / 4) * 20000 + 0.5),
                errors = 200 + i * 10,
                p50 = 45 + i,
                p95 = 120 + i * 3,
                p99 = 450 + i * 5,
            }
        end),
    }
end

local function audit_log()
    local actions = {
        'user.login',
        'user.logout',
        'service.deploy',
        'service.scale',
        'alert.ack',
        'api_key.create',
        'api_key.revoke',
        'settings.update',
        'member.invite',
        'member.remove',
    }
    local actors = {
        'Alice',
        'Bob',
        'Carol',
        'David',
        'Eve',
    }
    return {
        entries = generate(50, function(i)
            return {
                id = format('audit-%d', i + 1),
                actor = format('usr-%04d', i % 10 + 1),
                actorName = pick(actors, i),
                action = pick(actions, i),
                resource = format('svc-%03d', i % 40 + 1),
                ip = format('10.0.%d.%d', i % 255, (i * 3) % 255),
                at = NOW - i * 180000,
                success = i % 8 ~= 0,
                meta = i % 4 == 0 and {
                    reason = 'manual override',
                    ticket = format('TKT-%d', 1000 + i),
                } or NULL,
            }
        end),
    }
end

local data = {
    ['services-list'] = services_list(),
    ['alert-feed'] = alert_feed(),
    ['user-management'] = user_management(),
    ['metrics-overview'] = metrics_overview(),
    ['audit-log'] = audit_log(),
}

-- The dashboard merges every panel's data into one table.
local dashboard = {
    org = data['services-list'].org,
    services = data['services-list'].services,
    summaryStats = data['services-list'].summaryStats,
    activeCount = data['alert-feed'].activeCount,
    alerts = data['alert-feed'].alerts,
    pagination = data['user-management'].pagination,
    users = data['user-management'].users,
    entries = data['audit-log'].entries,
}
for k, v in pairs(data['metrics-overview']) do
    dashboard[k] = v
end
data.dashboard = dashboard

return data
