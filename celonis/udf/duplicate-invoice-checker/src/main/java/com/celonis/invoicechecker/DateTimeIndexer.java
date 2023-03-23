package com.celonis.invoicechecker;

import org.json.JSONObject;

import java.util.*;

public class DateTimeIndexer implements IndexerInterface {
    private static final int priority = 1;

    private final Map<String, Set<String>> connectedEdges = new HashMap<>();

    private int numDays = 7;
    public String columnName = "";
    private long MILLI_SECONDS_IN_WEEK = numDays * 24 * 3600 * 1000;


    public DateTimeIndexer() {
    }

    public DateTimeIndexer(String columnName) {
        this.columnName = columnName;
    }

    public DateTimeIndexer(String columnName, JSONObject params) {
        this.columnName = columnName;
        if (params == null) return;
        if (params.has("num_days")) {
            numDays = params.getInt("num_days");
        }
    }

    private void addTimeToId(int time, String id, Map<Integer, Set<String>> mapping) {
        if (mapping.containsKey(time)) {
            mapping.get(time).add(id);
        } else {
            Set<String> ids = new HashSet<>();
            ids.add(id);
            mapping.put(time, ids);
        }
    }

    public void index(List<ClusterObjectInterface> clusterObjects) {
        Map<String, ClusterObjectInterface> idToObjects = new HashMap<>();
        Map<String, Set<String>> potentialConnectedEdges = new HashMap<>();
        Map<Integer, Set<String>> yearToIds = new HashMap<>();
        Map<Integer, Set<String>> monthToIds = new HashMap<>();
        Map<Integer, Set<String>> dayToIds = new HashMap<>();
        List<Utils.ValueToId<Long>> timestampToIds = new ArrayList<>();

        for (ClusterObjectInterface clusterObject : clusterObjects) {
            DateTimeMatcher.DateTime dateTime = (DateTimeMatcher.DateTime) clusterObject;
            idToObjects.put(dateTime.getId(), dateTime);
            Calendar calendar = dateTime.getCalendar();
            int year = calendar.get(Calendar.YEAR);
            addTimeToId(year, dateTime.getId(), yearToIds);
            // Month is zero-based.
            int month = calendar.get(Calendar.MONTH) + 1;
            addTimeToId(month, dateTime.getId(), monthToIds);
            int day = calendar.get(Calendar.DAY_OF_MONTH);
            addTimeToId(day, dateTime.getId(), dayToIds);
            timestampToIds.add(new Utils.ValueToId(calendar.getTimeInMillis(), dateTime.getId()));
        }

        Collections.sort(timestampToIds, (left, right) -> left.getValue().compareTo(right.getValue()));

        for (String id : idToObjects.keySet()) {
            DateTimeMatcher.DateTime dateTime = (DateTimeMatcher.DateTime) idToObjects.get(id);
            Calendar calendar = dateTime.getCalendar();
            int year = calendar.get(Calendar.YEAR);
            int month = calendar.get(Calendar.MONTH) + 1;
            int day = calendar.get(Calendar.DAY_OF_MONTH);

            Set<String> yearMatchIds = yearToIds.get(year);
            Set<String> monthMatchIds = monthToIds.get(month);
            Set<String> dayMatchIds = dayToIds.get(day);

            // Add exact match date ids.
            {
                if (dayMatchIds != null && monthMatchIds != null && dayMatchIds != null) {
                    Set<String> matchIds = new HashSet<>(dayMatchIds);
                    matchIds.retainAll(monthMatchIds);
                    matchIds.retainAll(yearMatchIds);
                    Utils.addEdges(id, matchIds, potentialConnectedEdges);
                }
            }

            // Matching invoices within 7 days.
            {
                List<String> matchIds = new ArrayList<>();
                Utils.Range range = Utils.findRange(calendar.getTimeInMillis()-MILLI_SECONDS_IN_WEEK,
                        calendar.getTimeInMillis() + MILLI_SECONDS_IN_WEEK, timestampToIds);
                for (int i = range.getLeft(); i < range.getRight(); i++) {
                    matchIds.add(timestampToIds.get(i).getId());
                }
                Utils.addEdges(id, matchIds, potentialConnectedEdges);
            }

            // Swapping month & date matches.
            {
                Set<String> monthMatchDay = dayToIds.get(month);
                Set<String> dayMatchMonth = monthToIds.get(day);
                if (monthMatchDay != null && dayMatchMonth != null) {
                    Set<String> matchIds = new HashSet<>(monthMatchDay);
                    matchIds.retainAll(dayMatchMonth);
                    Utils.addEdges(id, matchIds, potentialConnectedEdges);
                }
            }

            // Month typos.
            {
                if (yearMatchIds != null && dayMatchIds != null) {
                    Set<String> matchIds = new HashSet<>(dayMatchIds);
                    matchIds.retainAll(yearMatchIds);
                    Set<String> monthSwapMatchIds = null;
                    if (month == 6) {
                        monthSwapMatchIds = monthToIds.get(7);
                    } else if (month == 7) {
                        monthSwapMatchIds = monthToIds.get(6);
                    } else if (month == 9) {
                        monthSwapMatchIds = monthToIds.get(10);
                    } else if (month == 10) {
                        monthSwapMatchIds = monthToIds.get(9);
                    }
                    if (monthSwapMatchIds != null) {
                        matchIds.retainAll(monthSwapMatchIds);
                        Utils.addEdges(id, matchIds, potentialConnectedEdges);
                    }
                }
            }
        }

        for (String id : idToObjects.keySet()) {
            Set<String> connectedIds = new HashSet<>();
            for (String pointedId : potentialConnectedEdges.get(id)) {
                if (pointedId != id && idToObjects.get(id).isSimilar(idToObjects.get(pointedId))) {
                    connectedIds.add(pointedId);
                }
            }
            connectedEdges.put(id, connectedIds);
        }
    }

    public Set<String> findEdges(String id, Set<String> candidateIds) {
        Set<String> resSet = connectedEdges.get(id);
        if (candidateIds == null) return new HashSet<>(resSet);
        candidateIds.retainAll(resSet);
        return candidateIds;
    }

    public ClusterObjectInterface createClusterObjFromJson(JSONObject obj) {
        String rowId = obj.getString("id");
        String content = obj.getString(columnName);
        DateTimeMatcher.DateTime date = new DateTimeMatcher.DateTime(rowId, content);
        date.setNumDays(numDays);
        return DateTimeMatcher.preprocess(date);
    }

    public Integer getPriority() {
        return priority;
    }
}
